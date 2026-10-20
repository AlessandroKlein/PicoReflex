/**
 * @file   web_ui.cpp
 * @brief  Implementación del "frontend": generación de las páginas HTML.
 *
 * Este módulo separa la presentación (HTML/CSS) del backend HTTP. `render()`
 * SOLO lee el estado compartido (configuración, estadísticas y red) y produce
 * la página HTML completa; no modifica nada. El ruteo y el parseo de parámetros
 * (y su aplicación al estado) lo hace web_server.cpp.
 *
 * Páginas (rutas) generadas, con navegación común y CSS oscuro consistente:
 *   "/"        -> Home: red + batería + resumen de stats + tabla de tiempos + controles.
 *   "/seed"    -> Semilla del PRNG.
 *   "/game"    -> Dificultad, modo, velocidad, duración de ronda y cuenta regresiva.
 *   "/display" -> Habilitar/deshabilitar el OLED.
 *   "/wifi"    -> Credenciales Wi-Fi (SSID/contraseña).
 *   "/player"  -> Nombre del jugador.
 *
 * Seguridad: todo texto controlado por el usuario (nombre, SSID, contraseña) se
 * escapa con html_escape() antes de insertarse en el HTML (prevención de XSS).
 *
 * Compilación: C++17, sin excepciones ni RTTI (acorde al proyecto).
 */

#include "web_ui.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "config.hpp"

namespace web_ui {

// =============================================================================
// html_escape()
// =============================================================================
// Escapa < > & " ' a sus entidades HTML para evitar XSS al insertar datos
// controlados por el usuario dentro de atributos/texto del HTML.
void html_escape(const char* src, char* dst, size_t n) {
    if (dst == nullptr || n == 0) return;

    size_t i = 0;
    while (*src && (i + 1u) < n) {
        const char* rep = nullptr;
        switch (*src) {
            case '<':  rep = "&lt;";    break;
            case '>':  rep = "&gt;";    break;
            case '&':  rep = "&amp;";   break;
            case '"':  rep = "&quot;";  break;
            case '\'': rep = "&#39;";   break;
            default:   break;
        }
        if (rep) {
            while (*rep && (i + 1u) < n) dst[i++] = *rep++;
        } else {
            dst[i++] = *src;
        }
        src++;
    }
    dst[i] = '\0';
}

// =============================================================================
// Builder (mini "string builder" para armar el HTML de forma segura)
// =============================================================================
namespace {

struct Builder {
    char*  buf;
    size_t cap;
    size_t len = 0;

    // Agrega una cadena literal, recortando si no cabe (deja 1 byte para '\0').
    void append(const char* s) {
        if (buf == nullptr || cap == 0) return;
        size_t n = std::strlen(s);
        if (len + n >= cap) n = cap - len - 1u;
        std::memcpy(buf + len, s, n);
        len += n;
        buf[len] = '\0';
    }

    // Agrega una cadena formateada (printf-style). El buffer intermedio es lo
    // bastante grande para el campo más largo (contraseña escapada ~384 B).
    void printf(const char* fmt, ...) {
        if (buf == nullptr || cap == 0) return;
        char tmp[512];
        va_list a;
        va_start(a, fmt);
        vsnprintf(tmp, sizeof(tmp), fmt, a);
        va_end(a);
        append(tmp);
    }
};

} // namespace

// =============================================================================
// CSS oscuro compartido + encabezado + navegación + pie
// =============================================================================
namespace {

const char* kCSS =
    "body{font-family:system-ui,sans-serif;background:#111;color:#eee;"
    "max-width:680px;margin:0 auto;padding:16px;}"
    "h1{color:#0f0;}"
    "nav{background:#1c1c1c;border:1px solid #333;border-radius:8px;"
    "padding:10px;margin:8px 0 16px;}"
    "nav a{color:#0f0;text-decoration:none;margin-right:12px;}"
    "nav a.active{font-weight:bold;border-bottom:2px solid #0f0;}"
    ".card{background:#1c1c1c;border:1px solid #333;border-radius:8px;"
    "padding:12px;margin:10px 0;}"
    "label{display:inline-block;min-width:140px;margin:4px 0;}"
    "input,select{background:#222;color:#eee;border:1px solid #444;"
    "border-radius:4px;padding:4px;margin:2px 0;}"
    "button{background:#0a0;color:#000;border:0;border-radius:4px;"
    "padding:6px 12px;margin:4px 4px 4px 0;font-weight:bold;cursor:pointer;}"
    "button.stop{background:#c00;color:#fff;}"
    ".ok{color:#0f0;} .bad{color:#f00;}"
    "table{width:100%;border-collapse:collapse;margin:8px 0;}"
    "th,td{border:1px solid #333;padding:4px 8px;text-align:left;}"
    "th{background:#222;}";

// Escribe el <head> (con CSS) y abre <body>.
void render_head(Builder& b, const char* title) {
    b.append("<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>");
    b.append("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    b.append("<title>");
    b.append(title);
    b.append(" - PicoReflex</title><style>");
    b.append(kCSS);
    b.append("</style></head><body>");
}

// Barra de navegación común. `active` es la ruta actual (resaltada).
void render_nav(Builder& b, const char* active) {
    struct NavItem { const char* label; const char* href; };
    static const NavItem items[] = {
        {"Home",    "/"},
        {"Semilla", "/seed"},
        {"Juego",   "/game"},
        {"Display", "/display"},
        {"Wi-Fi",   "/wifi"},
        {"Jugador", "/player"},
    };

    b.append("<nav>");
    for (const NavItem& it : items) {
        bool is_active = (std::strcmp(active, it.href) == 0);
        b.printf("<a href='%s'%s>%s</a>", it.href,
                 is_active ? " class='active'" : "", it.label);
    }
    b.append("</nav>");
}

void render_footer(Builder& b) {
    b.append("</body></html>");
}

} // namespace

// =============================================================================
// Páginas
// =============================================================================
namespace {

// --- "/" Home ----------------------------------------------------------------
void render_home(Builder& b) {
    GameConfig   cfg = config_get();
    GameStats    st  = stats_get();
    SystemStatus sys = system_status_get();

    uint32_t avg_ms = st.valid_reactions
                          ? (uint32_t)(st.total_time_us / st.valid_reactions / 1000u)
                          : 0u;
    uint32_t best_ms = (st.best_time_us == UINT32_MAX) ? 0u : st.best_time_us / 1000u;
    uint32_t hit_rate = st.total_rounds
                          ? (uint32_t)((uint64_t)st.hits * 100u / st.total_rounds)
                          : 0u;

    render_head(b, "Home");
    b.append("<h1>PicoReflex</h1>");
    render_nav(b, "/");

    // Red.
    const char* net = sys.wifi_ap ? "Access Point"
                    : (sys.wifi_sta ? "Station" : "Sin conexión");
    b.append("<div class='card'><h3>Red</h3>");
    b.printf("<p>Modo: <b>%s</b> &nbsp; IP: <b>%s</b></p>", net, sys.ip);
    b.append("</div>");

    // Batería.
    b.append("<div class='card'><h3>Batería</h3>");
    if (sys.battery_pct < 15u) {
        b.printf("<p><span class='bad'>%u%% (%u mV) - BAJA</span></p>",
                 (unsigned)sys.battery_pct, (unsigned)sys.battery_mv);
    } else {
        b.printf("<p><span class='ok'>%u%%</span> (%u mV)</p>",
                 (unsigned)sys.battery_pct, (unsigned)sys.battery_mv);
    }
    b.append("</div>");

    // Resumen de estadísticas.
    b.append("<div class='card'><h3>Estadísticas</h3>");
    b.printf("<p>Promedio: <b>%u ms</b> &nbsp; Mejor: <b>%u ms</b></p>",
             (unsigned)avg_ms, (unsigned)best_ms);
    b.printf("<p>Aciertos: %u &nbsp; Fallos: %u &nbsp; Timeouts: %u</p>",
             (unsigned)st.hits, (unsigned)st.misses, (unsigned)st.timeouts);
    b.printf("<p>Tasa de acierto: <b>%u%%</b></p>", (unsigned)hit_rate);
    b.append("</div>");

    // Tabla de tiempos de reacción (history[0] = más reciente).
    b.append("<div class='card'><h3>Últimos tiempos de reacción</h3>");
    if (st.history_count == 0u) {
        b.append("<p>Sin datos todavía.</p>");
    } else {
        b.append("<table><tr><th>#</th><th>Tiempo (ms)</th></tr>");
        for (uint32_t i = 0; i < st.history_count && i < GameStats::HISTORY_SIZE; i++) {
            b.printf("<tr><td>%u</td><td>%u ms</td></tr>",
                     (unsigned)(i + 1u), (unsigned)st.history[i]);
        }
        b.append("</table>");
    }
    b.append("</div>");

    // Controles (GET a /start, /stop, /reset).
    b.append("<div class='card'><h3>Controles</h3>");
    b.append("<a href='/start'><button>Iniciar</button></a>");
    b.append("<a href='/stop'><button class='stop'>Detener</button></a>");
    b.append("<a href='/reset'><button>Reiniciar stats</button></a>");
    b.append("</div>");

    render_footer(b);
}

// --- "/seed" ----------------------------------------------------------------
void render_seed(Builder& b) {
    GameConfig cfg = config_get();

    render_head(b, "Semilla");
    b.append("<h1>Semilla</h1>");
    render_nav(b, "/seed");

    b.append("<div class='card'><h3>Semilla del PRNG</h3>");
    b.append("<form method='get' action='/seed'>");
    b.printf("<label>Semilla (0 = aleatoria):</label><input name='seed' value='%u'>",
             (unsigned)cfg.seed);
    b.append("<br><button type='submit'>Guardar</button></form>");
    b.append("<p>Con semilla 0 se usa entropía del hardware; con un valor fijo la "
             "secuencia de LEDs es reproducible (ideal para competencias).</p>");
    b.append("</div>");

    render_footer(b);
}

// --- "/game" ----------------------------------------------------------------
void render_game(Builder& b) {
    GameConfig cfg = config_get();

    render_head(b, "Juego");
    b.append("<h1>Configuración del juego</h1>");
    render_nav(b, "/game");

    b.append("<div class='card'><form method='get' action='/game'>");

    // Dificultad.
    b.append("<label>Dificultad:</label><select name='difficulty'>");
    for (int i = 0; i < (int)Difficulty::DIFF_COUNT; i++) {
        b.printf("<option value='%d'%s>%s (%u ms)</option>", i,
                 ((int)cfg.difficulty == i) ? " selected" : "",
                 difficulty_name((Difficulty)i),
                 (unsigned)difficulty_tolerance_ms((Difficulty)i));
    }
    b.append("</select><br>");

    // Modo de juego.
    b.append("<label>Modo:</label><select name='mode'>");
    for (int i = 0; i < (int)GameMode::MODE_COUNT; i++) {
        b.printf("<option value='%d'%s>%s</option>", i,
                 ((int)cfg.mode == i) ? " selected" : "",
                 game_mode_name((GameMode)i));
    }
    b.append("</select><br>");

    // Velocidad / tiempos.
    b.printf("<label>Vel. mín (ms):</label><input name='smin' value='%u'><br>",
             (unsigned)cfg.stimulus_min_ms);
    b.printf("<label>Vel. máx (ms):</label><input name='smax' value='%u'><br>",
             (unsigned)cfg.stimulus_max_ms);
    b.printf("<label>Ronda (s, TIMED):</label><input name='round' value='%u'><br>",
             (unsigned)cfg.round_seconds);
    b.printf("<label>Cuenta regresiva (s):</label><input name='delay' value='%u'><br>",
             (unsigned)cfg.start_delay_s);

    b.append("<button type='submit'>Guardar</button></form></div>");

    render_footer(b);
}

// --- "/display" -------------------------------------------------------------
void render_display(Builder& b) {
    GameConfig cfg = config_get();

    render_head(b, "Display");
    b.append("<h1>Display OLED</h1>");
    render_nav(b, "/display");

    b.append("<div class='card'><h3>Estado</h3>");
    b.printf("<p>OLED: <b>%s</b></p>", cfg.oled_enabled ? "Habilitado" : "Deshabilitado");

    // Dos botones de envío con name='oled': ?oled=1 y ?oled=0.
    b.append("<form method='get' action='/display'>");
    b.append("<button type='submit' name='oled' value='1'>Habilitar</button>");
    b.append("<button type='submit' name='oled' value='0' class='stop'>Deshabilitar</button>");
    b.append("</form>");
    b.append("<p>Apagar el OLED ahorra energía; toda la información sigue "
             "disponible en esta web.</p>");
    b.append("</div>");

    render_footer(b);
}

// --- "/wifi" ----------------------------------------------------------------
void render_wifi(Builder& b) {
    GameConfig cfg = config_get();

    // Escapa las credenciales antes de insertarlas en el HTML (anti-XSS).
    char ssid_esc[200];
    char pass_esc[400];
    html_escape(cfg.wifi_ssid, ssid_esc, sizeof(ssid_esc));
    html_escape(cfg.wifi_password, pass_esc, sizeof(pass_esc));

    render_head(b, "Wi-Fi");
    b.append("<h1>Wi-Fi</h1>");
    render_nav(b, "/wifi");

    b.append("<div class='card'><h3>Credenciales (modo Station)</h3>");
    b.append("<form method='get' action='/wifi'>");
    b.printf("<label>SSID:</label><input name='ssid' value='%s'><br>", ssid_esc);
    b.printf("<label>Contraseña:</label><input type='password' name='pass' value='%s'><br>",
             pass_esc);
    b.append("<button type='submit'>Guardar</button></form>");
    b.append("</div>");

    render_footer(b);
}

// --- "/player" --------------------------------------------------------------
void render_player(Builder& b) {
    GameConfig cfg = config_get();

    char name_esc[96];
    html_escape(cfg.user_name, name_esc, sizeof(name_esc));

    render_head(b, "Jugador");
    b.append("<h1>Jugador</h1>");
    render_nav(b, "/player");

    b.append("<div class='card'><h3>Nombre</h3>");
    b.append("<form method='get' action='/player'>");
    b.printf("<label>Nombre:</label><input name='name' value='%s' maxlength='15'><br>",
             name_esc);
    b.append("<button type='submit'>Guardar</button></form>");
    b.append("</div>");

    render_footer(b);
}

} // namespace

// =============================================================================
// render()
// =============================================================================
bool render(const char* path, char* out, size_t out_len) {
    if (path == nullptr || out == nullptr || out_len == 0) {
        return false;
    }

    Builder b{out, out_len};

    if (std::strcmp(path, "/") == 0) {
        render_home(b);
    } else if (std::strcmp(path, "/seed") == 0) {
        render_seed(b);
    } else if (std::strcmp(path, "/game") == 0) {
        render_game(b);
    } else if (std::strcmp(path, "/display") == 0) {
        render_display(b);
    } else if (std::strcmp(path, "/wifi") == 0) {
        render_wifi(b);
    } else if (std::strcmp(path, "/player") == 0) {
        render_player(b);
    } else {
        return false;   // Ruta desconocida.
    }

    return true;
}

} // namespace web_ui
