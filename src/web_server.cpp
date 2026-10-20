/**
 * @file   web_server.cpp
 * @brief  Servidor HTTP embebido (lwIP raw API) + conectividad STA/SoftAP.
 *
 * Conectividad:
 *  1. Se intenta STA con `cyw43_arch_wifi_connect_timeout_ms()` (10 s).
 *  2. Si falla, se deshabilita STA y se habilita SoftAP con IP fija 192.168.4.1.
 *  El LED onboard de la Pico W indica el estado: fijo = STA, parpadeo = AP.
 *
 * Servidor HTTP (API raw de lwIP, NO_SYS=1):
 *  - Se registran callbacks tcp_accept / tcp_recv / tcp_sent.
 *  - Las peticiones son GET; se parsea la URL y la query string.
 *  - Rutas:
 *      GET /            -> página de control/estadísticas.
 *      GET /config?...  -> aplica parámetros y devuelve la página.
 *      GET /start       -> control_send(START_ROUND).
 *      GET /stop        -> control_send(STOP_ROUND).
 *      GET /reset       -> control_send(RESET_STATS).
 *
 * Referencias:
 *  - lwIP raw TCP: https://www.nongnu.org/lwip/2_1_x/group__tcp__raw.html
 *  - pico_cyw43_arch: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#pico_cyw43_arch
 */

#include "web_server.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "lwip/init.h"
#include "lwip/tcp.h"

#include "config.hpp"
#include "display_driver.hpp"
#include "game_logic.hpp"
#include "battery_monitor.hpp"
#include "captive_portal.hpp"
#include "pin_defs.hpp"

namespace web {

// Forward declaration: la usa run() y se define más abajo.
void refresh_display();


// -----------------------------------------------------------------------------
// Constantes del Access Point de respaldo
// -----------------------------------------------------------------------------
#define HTTP_PORT     80u
#define AP_SSID       "PicoW-ReactionGame"
#define AP_PASSWORD   "12345678"   // WPA2 exige al menos 8 caracteres.
#define AP_IP         "192.168.4.1"
#define STA_TIMEOUT_MS 10000u      // Tiempo máximo intentando STA (10 s).

// -----------------------------------------------------------------------------
// Estado de la conexión (solo lo usa este módulo, en el Core 1)
// -----------------------------------------------------------------------------
static bool g_is_ap = false;        ///< true si quedó en modo Access Point.

// -----------------------------------------------------------------------------
// Buffer de respuesta HTTP (una conexión a la vez; ver nota en http_recv)
// -----------------------------------------------------------------------------
static char g_body[2048];

// =============================================================================
// Utilidades de parseo de la query string
// =============================================================================
namespace {

// Busca "key=" en la query y devuelve puntero al valor (o NULL).
const char* find_param(const char* query, const char* key) {
    size_t klen = std::strlen(key);
    const char* p = query;
    while (p && *p) {
        if (std::strncmp(p, key, klen) == 0 && p[klen] == '=') {
            return p + klen + 1;
        }
        const char* amp = std::strchr(p, '&');
        if (!amp) break;
        p = amp + 1;
    }
    return nullptr;
}

// Devuelve el valor entero de un parámetro, o `def` si no está.
int get_int_param(const char* query, const char* key, int def) {
    const char* v = find_param(query, key);
    if (!v || !*v) return def;
    return (int)std::strtol(v, nullptr, 10);
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodifica %XX y '+' -> espacio (URL encoding mínimo para formularios GET).
void url_decode(const char* src, char* dst, size_t n) {
    size_t i = 0;
    while (*src && i < n - 1u) {
        if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            int h = hexval(src[1]) * 16 + hexval(src[2]);
            if (h >= 0) { dst[i++] = (char)h; src += 3; }
            else        { dst[i++] = *src++; }
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// -----------------------------------------------------------------------------
// Mini "string builder" para armar el HTML de respuesta
// -----------------------------------------------------------------------------
struct Builder {
    char*  buf;
    size_t cap;
    size_t len = 0;

    void append(const char* s) {
        size_t n = std::strlen(s);
        if (len + n >= cap) n = cap - len - 1u;   // Recorta si no cabe.
        std::memcpy(buf + len, s, n);
        len += n;
        buf[len] = '\0';
    }

    void printf(const char* fmt, ...) {
        char tmp[128];
        va_list a;
        va_start(a, fmt);
        vsnprintf(tmp, sizeof(tmp), fmt, a);
        va_end(a);
        append(tmp);
    }
};

} // namespace

// =============================================================================
// Aplicación de configuración desde la query string
// Escapa los caracteres especiales de HTML para evitar XSS al insertar datos
// controlados por el usuario (el nombre del jugador) dentro del HTML de respuesta.
static void html_escape(const char* src, char* dst, size_t n) {
    size_t i = 0;
    while (*src && i + 1u < n) {
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
            while (*rep && i + 1u < n) dst[i++] = *rep++;
        } else {
            dst[i++] = *src;
        }
        src++;
    }
    dst[i] = '\0';
}

// =============================================================================
static void apply_config(const char* query) {
    GameConfig cfg = config_get();

    cfg.seed = (uint32_t)get_int_param(query, "seed", (int)cfg.seed);

    int diff = get_int_param(query, "difficulty", (int)cfg.difficulty);
    if (diff >= 0 && diff < (int)Difficulty::DIFF_COUNT) {
        cfg.difficulty = (Difficulty)diff;
    }
    int mode = get_int_param(query, "mode", (int)cfg.mode);
    if (mode >= 0 && mode < (int)GameMode::MODE_COUNT) {
        cfg.mode = (GameMode)mode;
    }

    // Velocidad entre estímulos: se acota a un rango sano (50..60000 ms) y se
    // garantiza smin <= smax, para que en game_logic el cálculo
    // `span = smax - smin + 1` (sin signo) no haga wrap con valores inválidos.
    uint32_t smin = (uint32_t)get_int_param(query, "smin", (int)cfg.stimulus_min_ms);
    uint32_t smax = (uint32_t)get_int_param(query, "smax", (int)cfg.stimulus_max_ms);
    if (smin == 0) smin = cfg.stimulus_min_ms;   // 0 = conservar valor actual.
    if (smax == 0) smax = cfg.stimulus_max_ms;
    if (smin < 50u)  smin = 50u;
    if (smin > 60000u) smin = 60000u;
    if (smax < smin) smax = smin;
    if (smax > 60000u) smax = 60000u;
    cfg.stimulus_min_ms = smin;
    cfg.stimulus_max_ms = smax;

    uint32_t rsec = (uint32_t)get_int_param(query, "round", (int)cfg.round_seconds);
    if (rsec == 0) rsec = cfg.round_seconds;
    if (rsec < 1u)      rsec = 1u;
    if (rsec > 3600u)   rsec = 3600u;
    cfg.round_seconds = rsec;

    const char* name = find_param(query, "name");
    if (name) url_decode(name, cfg.user_name, sizeof(cfg.user_name));

    config_set(cfg);
    // El núcleo 0 relee la configuración en cada iteración; no hace falta evento.
}

// =============================================================================
// Generación de la página HTML
// =============================================================================
static void build_page(const char* query, char* out, size_t out_len) {
    // Si la petición trae parámetros (formulario), aplica la configuración.
    if (query && *query) {
        apply_config(query);
    }

    GameConfig   cfg   = config_get();
    GameStats    st    = stats_get();
    SystemStatus sys   = system_status_get();

    // Promedio de reacción en ms (0 si no hay datos).
    uint32_t avg_ms = st.valid_reactions
                          ? (uint32_t)(st.total_time_us / st.valid_reactions / 1000u)
                          : 0u;
    uint32_t best_ms = (st.best_time_us == UINT32_MAX) ? 0u : st.best_time_us / 1000u;
    uint32_t hit_rate = st.total_rounds
                          ? (uint32_t)((uint64_t)st.hits * 100u / st.total_rounds)
                          : 0u;

    Builder b{out, out_len};

    b.append("<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>");
    b.append("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    b.append("<title>PicoReflex</title><style>");
    b.append("body{font-family:system-ui,sans-serif;background:#111;color:#eee;"
             "max-width:640px;margin:0 auto;padding:16px;}");
    b.append("h1{color:#0f0;} .card{background:#1c1c1c;border:1px solid #333;"
             "border-radius:8px;padding:12px;margin:10px 0;}");
    b.append("label{display:inline-block;min-width:110px;margin:4px 0;}");
    b.append("input,select{background:#222;color:#eee;border:1px solid #444;"
             "border-radius:4px;padding:4px;margin:2px 0;}");
    b.append("button{background:#0a0;color:#000;border:0;border-radius:4px;"
             "padding:6px 12px;margin:4px 4px 4px 0;font-weight:bold;cursor:pointer;}");
    b.append("button.stop{background:#c00;color:#fff;} .ok{color:#0f0;} .bad{color:#f00;}");
    b.append("</style></head><body>");

    b.append("<h1>PicoReflex</h1>");

    // --- Estadísticas ---
    b.append("<div class='card'><h3>Estadísticas</h3>");
    b.printf("<p>Promedio: <b>%u ms</b> &nbsp; Mejor: <b>%u ms</b></p>", avg_ms, best_ms);
    b.printf("<p>Aciertos: %u &nbsp; Fallos: %u &nbsp; Timeouts: %u</p>",
             st.hits, st.misses, st.timeouts);
    b.printf("<p>Tasa de acierto: <b>%u%%</b></p>", hit_rate);
    if (battery::is_low_battery(sys.battery_pct)) {
        b.printf("<p>Batería: <span class='bad'>%u%% (%u mV) - BAJA</span></p>",
                 (unsigned)sys.battery_pct, (unsigned)sys.battery_mv);
    } else {
        b.printf("<p>Batería: <span class='ok'>%u%%</span> (%u mV)</p>",
                 (unsigned)sys.battery_pct, (unsigned)sys.battery_mv);
    }
    b.append("</div>");

    // --- Red ---
    b.append("<div class='card'><h3>Red</h3>");
    b.printf("<p>Modo: <b>%s</b> &nbsp; IP: <b>%s</b></p>",
             sys.wifi_ap ? "Access Point" : (sys.wifi_sta ? "Station" : "---"), sys.ip);
    b.append("</div>");

    // --- Formulario de configuración ---
    b.append("<div class='card'><h3>Configuración</h3>");
    b.append("<form method='get' action='/config'>");
    b.printf("<label>Semilla (0=alea.):</label><input name='seed' value='%u'><br>", cfg.seed);

    b.append("<label>Dificultad:</label><select name='difficulty'>");
    for (int i = 0; i < (int)Difficulty::DIFF_COUNT; i++) {
        b.printf("<option value='%d'%s>%s (%u ms)</option>", i,
                 ((int)cfg.difficulty == i) ? " selected" : "",
                 difficulty_name((Difficulty)i), difficulty_tolerance_ms((Difficulty)i));
    }
    b.append("</select><br>");

    b.append("<label>Modo:</label><select name='mode'>");
    for (int i = 0; i < (int)GameMode::MODE_COUNT; i++) {
        b.printf("<option value='%d'%s>%s</option>", i,
                 ((int)cfg.mode == i) ? " selected" : "", game_mode_name((GameMode)i));
    }
    b.append("</select><br>");

    b.printf("<label>Vel. mín (ms):</label><input name='smin' value='%u'><br>",
             cfg.stimulus_min_ms);
    b.printf("<label>Vel. máx (ms):</label><input name='smax' value='%u'><br>",
             cfg.stimulus_max_ms);
    b.printf("<label>Ronda (s):</label><input name='round' value='%u'><br>",
             cfg.round_seconds);
    // Escapa el nombre antes de insertarlo en el HTML (evita XSS almacenado).
    char name_esc[96];
    html_escape(cfg.user_name, name_esc, sizeof(name_esc));
    b.printf("<label>Nombre:</label><input name='name' value='%s' maxlength='15'><br>",
             name_esc);

    b.append("<button type='submit'>Guardar</button></form>");
    b.append("</div>");

    // --- Controles ---
    b.append("<div class='card'><h3>Controles</h3>");
    b.append("<a href='/start'><button>Iniciar</button></a>");
    b.append("<a href='/stop'><button class='stop'>Detener</button></a>");
    b.append("<a href='/reset'><button>Reiniciar stats</button></a>");
    b.append("</div>");

    b.append("</body></html>");
}

// =============================================================================
// Servidor HTTP (API raw de lwIP)
// =============================================================================
namespace {

// Se llama cuando los datos ya fueron enviados (ACK) -> cierra la conexión.
err_t http_sent(void*, struct tcp_pcb* tpcb, u16_t) {
    tcp_close(tpcb);
    return ERR_OK;
}

// Se llama al recibir datos del cliente.
err_t http_recv(void*, struct tcp_pcb* tpcb, struct pbuf* p, err_t err) {
    if (p == nullptr) {
        // El cliente cerró la conexión.
        if (tpcb) tcp_close(tpcb);
        return ERR_OK;
    }

    // Copia la petición a un buffer local (solo interesa la primera línea).
    char request[512];
    size_t len = p->tot_len < sizeof(request) - 1u ? p->tot_len : sizeof(request) - 1u;
    pbuf_copy_partial(p, request, (u16_t)len, 0);
    request[len] = '\0';
    pbuf_free(p);

    // Nota de diseño: se usa un único buffer global g_body para la respuesta.
    // En NO_SYS=1 las callbacks de lwIP están serializadas, así que no hay
    // concurrencia real entre conexiones en este servidor de un solo hilo.

    // Extrae la ruta (entre "GET " y el siguiente espacio).
    char path[128] = "/";
    const char* query = nullptr;
    if (std::strncmp(request, "GET ", 4) == 0) {
        const char* start = request + 4;
        const char* end = std::strchr(start, ' ');
        size_t plen = end ? (size_t)(end - start) : std::strlen(start);
        if (plen >= sizeof(path)) plen = sizeof(path) - 1u;
        std::memcpy(path, start, plen);
        path[plen] = '\0';
        // Separa la query string (tras '?').
        char* q = std::strchr(path, '?');
        if (q) { *q = '\0'; query = q + 1; }
    }

    // Rutas de control.
    if (std::strncmp(path, "/start", 6) == 0) control_send(ControlEvent::START_ROUND);
    else if (std::strncmp(path, "/stop", 5) == 0) control_send(ControlEvent::STOP_ROUND);
    else if (std::strncmp(path, "/reset", 6) == 0) control_send(ControlEvent::RESET_STATS);

    // Genera el cuerpo de la respuesta.
    build_page(query, g_body, sizeof(g_body));

    // Arma cabeceras + cuerpo y envía.
    size_t body_len = std::strlen(g_body);
    char headers[256];
    int hlen = std::snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: %u\r\n"
        "\r\n", (unsigned)body_len);

    tcp_write(tpcb, headers, (u16_t)hlen, TCP_WRITE_FLAG_COPY);
    tcp_write(tpcb, g_body, (u16_t)body_len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    // Cierra la conexión una vez que los datos se hayan enviado.
    tcp_sent(tpcb, http_sent);
    return ERR_OK;
}

err_t http_accept(void*, struct tcp_pcb* newpcb, err_t err) {
    if (err != ERR_OK || newpcb == nullptr) return ERR_VAL;
    tcp_nagle_disable(newpcb);      // Evita retardos por el algoritmo de Nagle.
    tcp_recv(newpcb, http_recv);    // Registra el callback de recepción.
    return ERR_OK;
}

void http_server_init() {
    struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return;
    if (tcp_bind(pcb, IP_ADDR_ANY, HTTP_PORT) != ERR_OK) { tcp_close(pcb); return; }
    struct tcp_pcb* l = tcp_listen(pcb);
    if (!l) { tcp_close(pcb); return; }
    tcp_accept(l, http_accept);
}

} // namespace

// =============================================================================
// Inicialización y bucle del Core 1
// =============================================================================
void init() {
    // Medición de batería (ADC). Se hace en el Core 1 porque requiere el lock
    // del CYW43 (GPIO29 = VSYS/3 y WL_CLK compartidos); ver battery_monitor.cpp.
    battery::init();
}

// Apaga la pila de red (llamado antes de que el Core 0 entre en DORMANT).
static void network_down() {
    captive::stop();        // Libera el servidor DNS (captive portal) si estaba.
    cyw43_arch_deinit();
}

// Levanta la pila de red (STA -> SoftAP), el captive portal (si AP) y el
// servidor HTTP. Se llama al arrancar y de nuevo tras cada despertar de DORMANT.
static void network_up() {
    if (cyw43_arch_init()) {
        return;   // En hardware real no debería fallar.
    }

    g_is_ap = false;
    cyw43_arch_enable_sta_mode();

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                            CYW43_AUTH_WPA2_AES_PSK,
                                            STA_TIMEOUT_MS) == 0) {
        // Conectado a la red Wi-Fi.
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);  // LED fijo = STA.
        uint32_t a = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;
        char ip[16];
        std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                      a & 0xffu, (a >> 8) & 0xffu, (a >> 16) & 0xffu, (a >> 24) & 0xffu);
        system_status_update_network(true, false, ip);
    } else {
        // Falló STA -> conmuta a Access Point (SoftAP) + captive portal.
        g_is_ap = true;
        cyw43_arch_disable_sta_mode();
        cyw43_arch_enable_ap_mode(AP_SSID, AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
        system_status_update_network(false, true, AP_IP);
        captive::init();    // DNS en puerto 53 -> 192.168.4.1 (redirección).
    }

    http_server_init();
}

void run() {
    // Levanta la red la primera vez.
    network_up();
    bool net_up = true;

    // --- Bucle del Core 1 (display + LED indicador + batería + dormancia). ---
    uint32_t blink_last = 0;
    uint32_t batt_last = 0;
    bool blink_state = false;
    while (true) {
        // Protocolo de DORMANT: el Core 0 pide apagar el Wi-Fi.
        if (g_sleep_request) {
            if (net_up) {
                network_down();
                net_up = false;
                g_sleep_ready = true;   // Avisa al Core 0 que ya puede dormir.
            }
            // Espera activa: este núcleo se detendrá cuando el chip entre en
            // DORMANT (el ROSC se apaga y clk_sys se congela).
            tight_loop_contents();
            continue;
        }
        // Despertar: si la red estaba apagada, la re-inicializa.
        if (!net_up) {
            network_up();
            net_up = true;
            blink_last = (uint32_t)(time_us_32() / 1000u);
            batt_last  = blink_last;
        }

        uint32_t now_ms = (uint32_t)(time_us_32() / 1000u);

        // Parpadeo del LED onboard en modo AP.
        if (g_is_ap) {
            if (now_ms - blink_last > 250u) {
                blink_last = now_ms;
                blink_state = !blink_state;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, blink_state);
            }
        }

        // Muestrea la batería ~1 vez por segundo y publica en estado compartido.
        if (now_ms - batt_last > 1000u) {
            batt_last = now_ms;
            uint32_t mv  = battery::read_voltage_mv();
            uint8_t  pct = battery::voltage_to_percent(mv);
            system_status_update_battery(mv, pct);
        }

        refresh_display();
        sleep_ms(200);   // Refresco del display ~5 veces por segundo.
    }
}

// =============================================================================
// Actualización del display OLED
// =============================================================================
void refresh_display() {
    static bool initialized = false;
    if (!initialized) {
        initialized = display::init();
    }

    GameConfig   cfg = config_get();
    GameStats    st  = stats_get();
    SystemStatus sys = system_status_get();

    uint32_t avg_ms = st.valid_reactions
                          ? (uint32_t)(st.total_time_us / st.valid_reactions / 1000u)
                          : 0u;
    uint32_t best_ms = (st.best_time_us == UINT32_MAX) ? 0u : st.best_time_us / 1000u;

    display::clear();

    // Título.
    display::draw_string(0, 0, "PICOREFLEX");

    if (game::is_round_active()) {
        display::draw_printf(0, 1, "REAC:%ums", avg_ms);
        display::draw_printf(0, 2, "MEJOR:%ums", best_ms);
    } else {
        display::draw_string(0, 1, "PULSA BOTON");
        display::draw_string(0, 2, "O WEB->INICIO");
    }

    display::draw_printf(0, 3, "BAT:%u%%", (unsigned)sys.battery_pct);
    display::draw_printf(0, 4, "MODO:%s", game_mode_name(cfg.mode));
    display::draw_printf(0, 5, "SEED:%u", cfg.seed);

    if (sys.wifi_ap) {
        display::draw_string(0, 6, "WIFI:AP");
    } else if (sys.wifi_sta) {
        display::draw_string(0, 6, "WIFI:STA");
    } else {
        display::draw_string(0, 6, "WIFI:--");
    }
    display::draw_string(0, 7, sys.ip);

    display::show();
}

} // namespace web
