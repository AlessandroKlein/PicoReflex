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
#include "web_ui.hpp"
#include "pin_defs.hpp"

namespace web {

// Forward declaration: la usa run() y se define más abajo.
void refresh_display();


// -----------------------------------------------------------------------------
// Constantes del Access Point de respaldo
// -----------------------------------------------------------------------------
// Credenciales del AP (modo respaldo cuando falla la conexión STA):
//   SSID:     PicoW-ReactionGame
//   IP:       192.168.4.1
//   Seguridad: AP_OPEN 1 = abierto (sin clave); 0 = WPA2 con AP_PASSWORD.
//     Por defecto se usa ABIERTO: el AP WPA2 del CYW43 da problemas de
//     compatibilidad con varios teléfonos ("No fue posible conectarse a la red").
#define HTTP_PORT     80u
#define AP_SSID       "PicoW-ReactionGame"
#define AP_PASSWORD   "12345678"   // Clave WPA2 (solo si AP_OPEN = 0).
#define AP_OPEN       1            // 1 = AP abierto (recomendado), 0 = WPA2.
#define AP_IP         "192.168.4.1"
#define STA_TIMEOUT_MS 10000u      // Tiempo máximo intentando STA (10 s).

// -----------------------------------------------------------------------------
// Estado de la conexión (solo lo usa este módulo, en el Core 1)
// -----------------------------------------------------------------------------
static bool g_is_ap = false;        ///< true si quedó en modo Access Point.
static volatile bool g_wifi_reconnect = false; ///< true si hay que re-conectar Wi-Fi.

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

} // namespace

// =============================================================================
// Aplicación de configuración según la ruta
// =============================================================================
// Aplica los parámetros de la query string a la configuración según la página
// (`path`). Si cambió algún campo, envía CONFIG_CHANGED para que el Core 0 lo
// persista en Flash; si cambió el Wi-Fi, marca reconexión.
static void handle_config(const char* path, const char* query) {
    GameConfig cfg = config_get();
    bool changed = false;
    bool wifi_changed = false;

    if (std::strncmp(path, "/seed", 5) == 0) {
        cfg.seed = (uint32_t)get_int_param(query, "seed", (int)cfg.seed);
        changed = true;
    } else if (std::strncmp(path, "/game", 5) == 0) {
        int diff = get_int_param(query, "difficulty", (int)cfg.difficulty);
        if (diff >= 0 && diff < (int)Difficulty::DIFF_COUNT) {
            cfg.difficulty = (Difficulty)diff;
        }
        int mode = get_int_param(query, "mode", (int)cfg.mode);
        if (mode >= 0 && mode < (int)GameMode::MODE_COUNT) {
            cfg.mode = (GameMode)mode;
        }

        // Velocidad entre estímulos: acotada (50..60000 ms) y smin <= smax, para
        // que `span = smax - smin + 1` (sin signo) no haga wrap.
        uint32_t smin = (uint32_t)get_int_param(query, "smin", (int)cfg.stimulus_min_ms);
        uint32_t smax = (uint32_t)get_int_param(query, "smax", (int)cfg.stimulus_max_ms);
        if (smin == 0) smin = cfg.stimulus_min_ms;
        if (smax == 0) smax = cfg.stimulus_max_ms;
        if (smin < 50u)    smin = 50u;
        if (smin > 60000u) smin = 60000u;
        if (smax < smin)   smax = smin;
        if (smax > 60000u) smax = 60000u;
        cfg.stimulus_min_ms = smin;
        cfg.stimulus_max_ms = smax;

        uint32_t rsec = (uint32_t)get_int_param(query, "round", (int)cfg.round_seconds);
        if (rsec == 0)    rsec = cfg.round_seconds;
        if (rsec < 1u)    rsec = 1u;
        if (rsec > 3600u) rsec = 3600u;
        cfg.round_seconds = rsec;

        uint32_t delay = (uint32_t)get_int_param(query, "delay", (int)cfg.start_delay_s);
        if (delay > 10u) delay = 10u;   // Límite de la cuenta regresiva.
        cfg.start_delay_s = delay;

        changed = true;
    } else if (std::strncmp(path, "/display", 8) == 0) {
        int oled = get_int_param(query, "oled", cfg.oled_enabled ? 1 : 0);
        cfg.oled_enabled = (oled != 0);
        changed = true;
    } else if (std::strncmp(path, "/wifi", 5) == 0) {
        const char* ssid = find_param(query, "ssid");
        const char* pass = find_param(query, "pass");
        if (ssid) {
            url_decode(ssid, cfg.wifi_ssid, sizeof(cfg.wifi_ssid));
            wifi_changed = true;
        }
        if (pass) {
            url_decode(pass, cfg.wifi_password, sizeof(cfg.wifi_password));
            wifi_changed = true;
        }
        changed = wifi_changed;
    } else if (std::strncmp(path, "/player", 7) == 0) {
        const char* name = find_param(query, "name");
        if (name) {
            url_decode(name, cfg.user_name, sizeof(cfg.user_name));
            changed = true;
        }
    }

    if (changed) {
        config_set(cfg);
        control_send(ControlEvent::CONFIG_CHANGED);  // Persistir en Flash (Core 0).
        if (wifi_changed) {
            g_wifi_reconnect = true;                 // Reconectar en el bucle del Core 1.
        }
    }
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

    // Rutas de control: encolan el evento y devuelven el Home.
    if (std::strncmp(path, "/start", 6) == 0) {
        control_send(ControlEvent::START_ROUND);
        web_ui::render("/", g_body, sizeof(g_body));
    } else if (std::strncmp(path, "/stop", 5) == 0) {
        control_send(ControlEvent::STOP_ROUND);
        web_ui::render("/", g_body, sizeof(g_body));
    } else if (std::strncmp(path, "/reset", 6) == 0) {
        control_send(ControlEvent::RESET_STATS);
        web_ui::render("/", g_body, sizeof(g_body));
    } else {
        // Páginas de configuración: aplica los parámetros y renderiza la página.
        handle_config(path, query);
        if (!web_ui::render(path, g_body, sizeof(g_body))) {
            web_ui::render("/", g_body, sizeof(g_body));   // Ruta desconocida -> Home.
        }
    }

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
    // Medición de batería (ADC, divisor externo en GPIO26). Se hace en el Core 1
    // porque es quien publica el estado (batería) para la web y el display.
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

    // Diagnóstico por USB (stdio). Si el usuario mantiene el botón 0 al arrancar,
    // se fuerza AP sin intentar STA.
    GameConfig cfg = config_get();
    bool sta_ok = false;

    if (!g_force_ap) {
        cyw43_arch_enable_sta_mode();
        printf("[web] Intentando STA: %s ...\n", cfg.wifi_ssid);
        sta_ok = (cyw43_arch_wifi_connect_timeout_ms(cfg.wifi_ssid, cfg.wifi_password,
                                                      CYW43_AUTH_WPA2_AES_PSK,
                                                      STA_TIMEOUT_MS) == 0);
    } else {
        printf("[web] STA omitido (forzar AP)\n");
    }

    if (sta_ok) {
        // Conectado a la red Wi-Fi.
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);  // LED fijo = STA.
        uint32_t a = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;
        char ip[16];
        std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                      a & 0xffu, (a >> 8) & 0xffu, (a >> 16) & 0xffu, (a >> 24) & 0xffu);
        system_status_update_network(true, false, ip);
        printf("[web] Conectado a STA, IP %s\n", ip);
    } else {
        // Falló STA -> conmuta a Access Point (SoftAP) + captive portal.
        g_is_ap = true;
        cyw43_arch_disable_sta_mode();
#if AP_OPEN
        cyw43_arch_enable_ap_mode(AP_SSID, nullptr, CYW43_AUTH_OPEN);
        printf("[web] Modo AP (ABIERTO): %s / IP %s\n", AP_SSID, AP_IP);
#else
        cyw43_arch_enable_ap_mode(AP_SSID, AP_PASSWORD, CYW43_AUTH_WPA2_MIXED_PSK);
        printf("[web] Modo AP: %s / clave %s / IP %s\n", AP_SSID, AP_PASSWORD, AP_IP);
#endif
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

        // Re-conexión Wi-Fi solicitada desde la web (cambio de credenciales).
        if (g_wifi_reconnect) {
            g_wifi_reconnect = false;
            network_down();
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
    GameConfig cfg = config_get();

    // OLED deshabilitado (configurable vía web): apaga el display y no dibuja.
    // La misma información está disponible en la página web.
    if (!cfg.oled_enabled) {
        display::set_power(false);
        return;
    }

    static bool initialized = false;
    if (!initialized) {
        initialized = display::init();
    }
    display::set_power(true);

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
