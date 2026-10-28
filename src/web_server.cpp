/**
 * @file   web_server.cpp
 * @brief  Servidor HTTP embebido (lwIP raw API) + conectividad STA/SoftAP.
 *
 * Conectividad:
 *  1. Se intenta STA con `cyw43_arch_wifi_connect_timeout_ms()` (10 s).
 *  2. Si falla, se deshabilita STA y se habilita SoftAP con IP fija 192.168.4.1.
 *  El LED onboard de la Pico W indica el estado: fijo = STA, parpadeo = AP.
 *
 * Servidor HTTP (API raw de lwIP, NO_SYS=0 / sys_freertos):
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

#include "FreeRTOS.h"
#include "task.h"

#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "hardware/watchdog.h"
#include "lwip/init.h"
#include "lwip/tcp.h"
#include "lwip/ip4_addr.h"
#include "lwip/apps/mdns.h"

#include "config.hpp"
#include "display_driver.hpp"
#include "game_logic.hpp"
#include "battery_monitor.hpp"
#include "captive_portal.hpp"
#include "dhcpserver.h"
#include "web_ui.hpp"
#include "pin_defs.hpp"
#include "flash_storage.hpp"   // flash::ota_write_sector / ota_write_metadata
#include "ota.h"               // offsets, metadatos y CRC32 de OTA
#include "hardware/flash.h"    // FLASH_SECTOR_SIZE

namespace web {

// Forward declaration: la usa run() y se define más abajo.
void refresh_display();


// -----------------------------------------------------------------------------
// Constantes del Access Point de respaldo
// -----------------------------------------------------------------------------
// Credenciales del AP (modo respaldo cuando falla la conexión STA):
//   SSID:     PicoW-ReactionGame
//   IP:       192.168.4.1
//   Seguridad: AP_OPEN 1 = abierto (sin clave); 0 = WPA2 con clave. La clave ya
//     no tiene un valor por defecto conocido: se deriva del RUID de la Flash o se
//     configura desde /wifi (persistida en Flash). Históricamente el AP "no
//     conectaba" por falta de servidor DHCP (v0.2.9); con DHCP ya funciona WPA2.
#define HTTP_PORT     80u
#define AP_SSID       "PicoW-ReactionGame"
#define AP_OPEN       0            // 0 = WPA2 (clave derivada del RUID o configurada), 1 = abierto.
#define AP_CHANNEL    1            // Canal 2.4 GHz fijo (1, 6 u 11). Estable.
#define AP_IP         "192.168.4.1"
#define STA_TIMEOUT_MS 10000u      // Tiempo máximo intentando STA (10 s).

// --- Re-conexión Wi-Fi en caliente (máquina de estados STA -> AP) ---
// Cuando el enlace STA se cae DURANTE la operación (no solo al arrancar), el
// bucle vigila cyw43_wifi_link_status(CYW43_ITF_STA) y, si es distinto de
// CYW43_LINK_JOIN, reintenta con backoff exponencial. Tras N intentos fallidos
// consecutivos, degrada a SoftAP (captive portal + DHCP ya listos).
#define STA_MAX_RETRIES      3       ///< Reintentos antes de degradar a AP.
#define STA_BACKOFF_BASE_MS  1000u   ///< Backoff inicial (1 s).
#define STA_BACKOFF_MAX_MS   15000u  ///< Tope del backoff (15 s).
#define STA_LINK_CHECK_MS    1000u   ///< Periodicidad del watchdog (1 s).

// --- mDNS (picoreflex.local) -------------------------------------------------
// Publica el hostname picoreflex.local en modo STA (mDNS responder de lwIP).
// En modo AP no se registra: el cliente accede por http://192.168.4.1. Requiere
// LWIP_IGMP + LWIP_MDNS_RESPONDER en lwipopts.h y pico_lwip_mdns en CMakeLists.
#define MDNS_HOSTNAME        "picoreflex"
#define MDNS_SERVICE_NAME    "PicoReflex"
#define MDNS_SERVICE_TYPE    "_http"

// -----------------------------------------------------------------------------
// Estado de la conexión (solo lo usa este módulo, en el Core 1)
// -----------------------------------------------------------------------------
static bool g_is_ap = false;        ///< true si quedó en modo Access Point.
static char g_ap_password[16] = ""; ///< Clave WPA2 efectiva del AP (RUID o configurada).
static volatile bool g_wifi_reconnect = false; ///< true si hay que re-conectar Wi-Fi.
static volatile bool g_ota_reboot_pending = false; ///< true si hay que reiniciar tras OTA.

// Estado de la máquina de re-conexión STA (vigilancia del enlace en caliente).
static uint8_t  g_sta_retries = 0;        ///< Reintentos consecutivos fallidos.
static uint32_t g_sta_backoff_ms = 0;     ///< Backoff actual (se duplica cada fallo).
static uint32_t g_sta_backoff_until = 0;  ///< Marca de tiempo del próximo reintento.
static uint32_t g_link_last_check = 0;    ///< Última vez que se vigiló el enlace.

// Estado mDNS (solo se registra en modo STA).
static bool g_mdns_inited = false;         ///< mdns_resp_init() ya ejecutado.
static bool g_mdns_sta_registered = false; ///< netif STA registrado (picoreflex.local).

// Servidor DHCP para el modo AP: sin él, el cliente se asocia pero no recibe IP
// (por eso el teléfono "conectaba y se desconectaba"). Implementación de
// MicroPython/pico-examples (MIT), ver dhcpserver.c.
static dhcp_server_t g_dhcp_server;

// Inicia el servidor DHCP en la interfaz del AP (192.168.4.1, máscara /24).
// Se ejecuta desde la tarea de red (network_task), NO desde el hilo tcpip, así
// que la API raw de lwIP debe ir bajo cyw43_arch_lwip_begin/end (equivale al
// LOCK_TCPIP_CORE del SDK). Sin el lock, dhcp_server_init() (udp_new/udp_bind/
// udp_recv/udp_bind_netif) corre en paralelo con el hilo tcpip y corrompe la
// lista global udp_pcbs: cuelgue duro -> reset por watchdog al asociar cliente.
static void ap_dhcp_start() {
    ip4_addr_t gw, mask;
    IP4_ADDR(&gw, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    cyw43_arch_lwip_begin();
    dhcp_server_init(&g_dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);
    cyw43_arch_lwip_end();
}

// Detiene el servidor DHCP del AP.
static void ap_dhcp_stop() {
    cyw43_arch_lwip_begin();
    dhcp_server_deinit(&g_dhcp_server);
    cyw43_arch_lwip_end();
}

// -----------------------------------------------------------------------------
// Escaneo de redes Wi-Fi (para autocompletar el SSID en /wifi)
// -----------------------------------------------------------------------------
// El escaneo es asíncrono (los resultados llegan por callback en el contexto de
// bajo nivel del CYW43). Por eso NO se hace dentro de http_recv (que corre en ese
// contexto): se pide con un flag y el bucle principal (Core 1) lo ejecuta,
// bloqueando de forma segura y guardando el resultado en un caché.
#define SCAN_MAX_SSIDS 20
static volatile bool g_scan_requested = false;    ///< El bucle principal debe escanear.
static volatile int  g_scan_count = 0;            ///< SSIDs encontrados en el caché.
static char  g_scan_ssids[SCAN_MAX_SSIDS][33];    ///< Caché de SSIDs (máx 32+1).

// Callback del escaneo: acumula SSIDs únicos (corre en el contexto del CYW43).
static int scan_result_cb(void* env, const cyw43_ev_scan_result_t* result) {
    (void)env;
    if (result == nullptr || result->ssid_len == 0) return 0;
    int len = (result->ssid_len < 32) ? result->ssid_len : 32;
    // Ignora duplicados.
    for (int i = 0; i < g_scan_count; i++) {
        if (std::strncmp(g_scan_ssids[i], (const char*)result->ssid, len) == 0 &&
            g_scan_ssids[i][len] == '\0') return 0;
    }
    if (g_scan_count >= SCAN_MAX_SSIDS) return 0;
    std::memcpy(g_scan_ssids[g_scan_count], result->ssid, len);
    g_scan_ssids[g_scan_count][len] = '\0';
    g_scan_count++;
    return 0;
}

// Ejecuta un escaneo (SOLO desde el bucle principal del Core 1) y actualiza el caché.
static void wifi_scan_now() {
    g_scan_count = 0;
    int rc = cyw43_wifi_scan(&cyw43_state, nullptr, nullptr, scan_result_cb);
    printf("[web] scan iniciado (rc=%d)\n", rc);
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (cyw43_wifi_scan_active(&cyw43_state)) {
        if (to_ms_since_boot(get_absolute_time()) - start > 5000u) break;  // Timeout 5 s.
        vTaskDelay(pdMS_TO_TICKS(20));
        watchdog_update();   // El escaneo bloquea hasta ~5 s: refresca el watchdog.
    }
    printf("[web] scan terminado: %d redes\n", g_scan_count);
}

// -----------------------------------------------------------------------------
// Buffer de respuesta HTTP (una conexión a la vez; ver nota en http_recv)
// -----------------------------------------------------------------------------
static char g_body[8192];   // Buffer de respuesta HTTP (HTML). La Home mide ~4.4 KB
                            // con el CSS nuevo; 8192 da margen para contraseñas/
                            // nombres largos escapados sin truncar la página.

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
// Se detiene en '&' (separador de parámetros): el valor de un parámetro va hasta
// el siguiente '&' o el final de la query. Sin esto, el SSID capturaba el resto
// ("ssid&pass=...").
void url_decode(const char* src, char* dst, size_t n) {
    size_t i = 0;
    while (*src && *src != '&' && i < n - 1u) {
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
// Autenticación (PIN) y rate-limiting de los endpoints de control
// =============================================================================
namespace {

// PIN de acceso a los endpoints de control/configuración. Sobrescribible en
// tiempo de compilación con -DWEB_PIN="..." (por ejemplo en CMakeLists.txt).
// Por defecto "1234"; es responsabilidad del integrador cambiarlo en producción.
#ifndef WEB_PIN
#define WEB_PIN "1234"
#endif

// Rate limiting de ventana fija (anti-flood y anti fuerza bruta).
#define RATE_WINDOW_MS     1000u   // Ventana global: 1 s.
#define RATE_MAX_REQS      60u     // Máx. peticiones/s (todas las rutas).
#define LOGIN_WINDOW_MS    1000u   // Ventana del login: 1 s.
#define LOGIN_MAX_REQS     5u      // Máx. intentos de login/s (anti fuerza bruta).

// Estado del rate-limiter. El servidor es de un solo hilo (callbacks lwIP
// serializadas, NO_SYS=0), así que no se necesita exclusión mutua.
static uint32_t g_rate_window_start = 0;
static uint32_t g_rate_count = 0;
static uint32_t g_login_window_start = 0;
static uint32_t g_login_count = 0;

// Token de sesión (FNV-1a + sal del PIN), en hex, para la cookie. Así la cookie
// no expone el PIN en claro en el navegador. Se calcula de forma perezosa.
static char g_auth_token[9] = "";

uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s; h *= 16777619u; s++; }
    return h;
}

const char* auth_token() {
    if (g_auth_token[0] == '\0') {
        uint32_t h = fnv1a(WEB_PIN) ^ 0x9e3779b9u;
        std::snprintf(g_auth_token, sizeof(g_auth_token), "%08x", (unsigned)h);
    }
    return g_auth_token;
}

// Comparación en tiempo constante (evita el timing side-channel al validar PIN).
bool pin_equal(const char* a, const char* b) {
    size_t la = std::strlen(a), lb = std::strlen(b);
    uint8_t diff = 0;
    size_t i = 0;
    while (i < la || i < lb) {
        char ca = (i < la) ? a[i] : '\0';
        char cb = (i < lb) ? b[i] : '\0';
        diff |= (uint8_t)(ca ^ cb);
        i++;
    }
    return diff == 0;
}

// Comparación de prefijo sin distinguir mayúsculas (nombres de cabeceras HTTP).
bool ieq_prefix(const char* s, const char* prefix) {
    while (*prefix) {
        char a = *s;
        char b = *prefix;
        if (a >= 'A' && a <= 'Z') a += ('a' - 'A');
        if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
        if (a != b) return false;
        s++; prefix++;
    }
    return true;
}

// Busca una cabecera HTTP (case-insensitive) y devuelve un puntero al primer
// carácter de su valor (tras ':'). nullptr si no existe. Se asume CRLF (\r\n).
const char* find_header(const char* req, const char* name) {
    size_t nlen = std::strlen(name);
    const char* p = req;
    while (p && *p) {
        if (ieq_prefix(p, name) && p[nlen] == ':') {
            return p + nlen + 1;   // Salta "Name:".
        }
        const char* eol = std::strstr(p, "\r\n");
        if (!eol) break;
        p = eol + 2;               // Siguiente línea de cabecera.
    }
    return nullptr;
}

// Salta espacios en blanco (para "Authorization: Bearer <pin>").
const char* skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Valida la petición:
//   - Cookie: pr_auth=<token derivado del PIN>  (lo setea /login).
//   - Authorization: Bearer <PIN>               (para clientes/curl).
bool is_authorized(const char* req) {
    // Cookie.
    const char* cookie = find_header(req, "Cookie");
    if (cookie) {
        const char* p = cookie;
        while (p && *p) {
            p = skip_ws(p);
            if (*p == '\r' || *p == '\n' || *p == '\0') break;
            if (std::strncmp(p, "pr_auth", 7) == 0 && p[7] == '=') {
                const char* v = p + 8;
                const char* end = v;
                while (*end && *end != ';' && *end != '\r' && *end != '\n') end++;
                size_t vlen = (size_t)(end - v);
                return (vlen == 8 && std::strncmp(v, auth_token(), 8) == 0);
            }
            const char* semi = std::strchr(p, ';');
            if (!semi) break;
            p = semi + 1;
        }
    }

    // Cabecera Authorization: acepta "Bearer <PIN>".
    const char* authz = find_header(req, "Authorization");
    if (authz) {
        authz = skip_ws(authz);
        if (ieq_prefix(authz, "Bearer")) {
            const char* cred = skip_ws(authz + 6);
            const char* end = cred;
            while (*end && *end != '\r' && *end != '\n') end++;
            while (end > cred && (end[-1] == ' ' || end[-1] == '\t')) end--;
            char buf[32];
            size_t n = (size_t)(end - cred);
            if (n >= sizeof(buf)) n = sizeof(buf) - 1u;
            std::memcpy(buf, cred, n);
            buf[n] = '\0';
            if (pin_equal(buf, WEB_PIN)) return true;
        }
    }
    return false;
}

// Rutas protegidas: verbos de control + páginas de configuración.
bool is_protected_path(const char* path) {
    return std::strncmp(path, "/start", 6) == 0 ||
           std::strncmp(path, "/stop", 5) == 0 ||
           std::strncmp(path, "/reset", 6) == 0 ||
           std::strncmp(path, "/seed", 5) == 0 ||
           std::strncmp(path, "/game", 5) == 0 ||
           std::strncmp(path, "/display", 8) == 0 ||
           std::strncmp(path, "/wifi", 5) == 0 ||
           std::strncmp(path, "/battery", 8) == 0 ||
           std::strncmp(path, "/config", 7) == 0 ||
           std::strncmp(path, "/ota", 4) == 0;
}

// Rate-limiter de ventana fija: devuelve true si se excedió el límite.
bool rate_limited(uint32_t* window_start, uint32_t* count,
                  uint32_t window_ms, uint32_t max_reqs) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - *window_start) >= window_ms) {
        *window_start = now;
        *count = 0;
    }
    (*count)++;
    return (*count) > max_reqs;
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

        // Controlador del panel: 0 = SSD1306, 1 = SH1106 (validado a 0..1).
        int ctrl = get_int_param(query, "controller", (int)cfg.display_controller);
        cfg.display_controller = (ctrl == 1) ? 1 : 0;

        // Modo dormir: habilitar/deshabilitar + minutos de inactividad (1..60).
        cfg.sleep_enabled = get_int_param(query, "sleep", cfg.sleep_enabled ? 1 : 0) != 0;
        uint32_t idle = (uint32_t)get_int_param(query, "idle_min", (int)cfg.idle_sleep_min);
        if (idle < 1u)  idle = 1u;
        if (idle > 60u) idle = 60u;
        cfg.idle_sleep_min = idle;

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

        // Clave del AP de respaldo: vacío = volver a la derivada del RUID;
        // 8..15 caracteres = valor WPA2 válido. Longitudes 1..7 se ignoran.
        const char* ap_pass = find_param(query, "ap_pass");
        if (ap_pass) {
            char ap_buf[16];
            url_decode(ap_pass, ap_buf, sizeof(ap_buf));
            size_t ap_len = std::strlen(ap_buf);
            if (ap_len == 0u || ap_len >= 8u) {
                std::strncpy(cfg.ap_password, ap_buf, sizeof(cfg.ap_password) - 1u);
                cfg.ap_password[sizeof(cfg.ap_password) - 1u] = '\0';
                changed = true;   // No marca wifi_changed: el AP no se reinicia en vivo.
            }
        }
    } else if (std::strncmp(path, "/battery", 8) == 0) {
        // Calibración de la curva de descarga: 3 puntos (mV) + compensación de carga.
        uint32_t vf = (uint32_t)get_int_param(query, "vfull",   (int)cfg.batt_calib.v_full_mv);
        uint32_t vm = (uint32_t)get_int_param(query, "vmid",    (int)cfg.batt_calib.v_mid_mv);
        uint32_t ve = (uint32_t)get_int_param(query, "vempty",  (int)cfg.batt_calib.v_empty_mv);
        uint32_t lc = (uint32_t)get_int_param(query, "loadcomp",(int)cfg.batt_calib.load_comp_mv);

        // Rango sano para una celda LiPo/Li-Ion (1S) y orden estricto ve < vm < vf,
        // para que la interpolación por tramos no divida por cero ni haga wrap.
        if (vf < 2500u) vf = 2500u; else if (vf > 5000u) vf = 5000u;
        if (ve < 2000u) ve = 2000u; else if (ve > 4500u) ve = 4500u;
        if (ve + 2u > vf) {                       // Sin hueco para vm -> por defecto.
            vf = 4200u; vm = 3700u; ve = 3000u;
        } else if (vm <= ve || vm >= vf) {
            vm = ve + (vf - ve) / 2u;             // vm fuera de rango -> re-centrar.
        }
        if (lc > 500u) lc = 500u;

        cfg.batt_calib.v_full_mv    = vf;
        cfg.batt_calib.v_mid_mv     = vm;
        cfg.batt_calib.v_empty_mv   = ve;
        cfg.batt_calib.load_comp_mv = lc;
        changed = true;
    }

    if (changed) {
        config_set(cfg);
        // Marca "persistencia pendiente" ANTES de encolar el evento: así el bucle
        // de reconexión espera a que el Core 0 termine de escribir en Flash (evita
        // que el guardado y el cyw43_arch_deinit/init corran a la vez y crasheen).
        g_persist_done = false;
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

// Envía una respuesta HTTP simple (status + texto opcional) y cierra la conexión.
// Se usa para el "captive portal check": responder lo que el cliente espera para
// que NO se desconecte de la red "sin internet". `extra_headers` (opcional) añade
// cabeceras adicionales ya terminadas en "\r\n" (Set-Cookie, WWW-Authenticate...).
static void send_http_response(struct tcp_pcb* tpcb, const char* status_line,
                               const char* content_type, const char* body,
                               const char* extra_headers = nullptr) {
    char headers[512];
    size_t body_len = body ? std::strlen(body) : 0;
    if (body) {
        int hlen = std::snprintf(headers, sizeof(headers),
            "HTTP/1.1 %s\r\nContent-Type: %s\r\n%sConnection: close\r\nContent-Length: %u\r\n\r\n",
            status_line, content_type, extra_headers ? extra_headers : "", (unsigned)body_len);
        tcp_write(tpcb, headers, (u16_t)hlen, TCP_WRITE_FLAG_COPY);
        tcp_write(tpcb, body, (u16_t)body_len, TCP_WRITE_FLAG_COPY);
    } else {
        int hlen = std::snprintf(headers, sizeof(headers),
            "HTTP/1.1 %s\r\n%sConnection: close\r\n\r\n",
            status_line, extra_headers ? extra_headers : "");
        tcp_write(tpcb, headers, (u16_t)hlen, TCP_WRITE_FLAG_COPY);
    }
    tcp_output(tpcb);
    tcp_sent(tpcb, http_sent);
}

// Envía una redirección 302 (p. ej. tras /login o /logout) y cierra la conexión.
// `extra_headers` opcional (p. ej. la cabecera Set-Cookie de la sesión).
static void send_redirect(struct tcp_pcb* tpcb, const char* location, const char* extra_headers = nullptr) {
    char headers[384];
    int hlen = std::snprintf(headers, sizeof(headers),
        "HTTP/1.1 302 Found\r\nLocation: %s\r\n%sConnection: close\r\n\r\n",
        location, extra_headers ? extra_headers : "");
    tcp_write(tpcb, headers, (u16_t)hlen, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    tcp_sent(tpcb, http_sent);
}

// =============================================================================
// OTA: recepción y grabación de firmware por streaming (POST /ota/upload)
// =============================================================================
// La imagen se recibe por POST (body binario) con su CRC32 esperado en la
// cabecera X-CRC32. Se graba sector a sector (4 KB) en el slot de STAGING
// calculando el CRC en caliente. Al terminar, si el CRC coincide, se escriben
// los metadatos con commit_state=PENDING y se reinicia; el bootloader de segundo
// nivel verifica el staging y lo copia al slot de aplicación (rollback si falla).

// Estado de la subida OTA (una subida a la vez; el servidor es de un solo hilo).
static struct tcp_pcb* g_ota_pcb = nullptr;
static bool     g_ota_headers_done = false;
static uint32_t g_ota_content_length = 0;
static uint32_t g_ota_received = 0;
static uint32_t g_ota_crc = 0;
static uint32_t g_ota_expected_crc = 0;
static bool     g_ota_have_expected_crc = false;
static uint32_t g_ota_sector_used = 0;
static uint32_t g_ota_sector_index = 0;
static uint8_t  g_ota_sector[FLASH_SECTOR_SIZE];   // staging de 4 KB (RAM).
static char     g_ota_header[512];                 // cabecera HTTP acumulada.
static uint32_t g_ota_header_len = 0;

// Reinicia el estado de la subida.
static void ota_upload_reset() {
    g_ota_pcb = nullptr;
    g_ota_headers_done = false;
    g_ota_content_length = 0;
    g_ota_received = 0;
    g_ota_crc = 0;
    g_ota_expected_crc = 0;
    g_ota_have_expected_crc = false;
    g_ota_sector_used = 0;
    g_ota_sector_index = 0;
    g_ota_header_len = 0;
    g_ota_header[0] = '\0';
}

// Aborta la subida con un error.
static void ota_upload_fail(struct tcp_pcb* tpcb, const char* msg) {
    ota_upload_reset();
    send_http_response(tpcb, "400 Bad Request", "text/plain", msg);
}

// Graba el sector acumulado (4 KB) en el slot de staging.
static bool ota_flush_sector() {
    if (g_ota_sector_used == 0) return true;
    // Rellena el resto con 0xFF (estado borrado) para programar el sector completo.
    std::memset(g_ota_sector + g_ota_sector_used, 0xFF,
                FLASH_SECTOR_SIZE - g_ota_sector_used);
    uint32_t offs = OTA_STAGING_OFFSET + g_ota_sector_index * FLASH_SECTOR_SIZE;
    bool ok = flash::ota_write_sector(offs, g_ota_sector);
    if (ok) {
        g_ota_sector_index++;
        g_ota_sector_used = 0;
    }
    return ok;
}

// Procesa un trozo del cuerpo (copia al sector y actualiza el CRC).
static bool ota_stream_chunk(struct tcp_pcb* tpcb, const uint8_t* data, uint32_t len) {
    uint32_t remaining = g_ota_content_length - g_ota_received;
    if (len > remaining) len = remaining;

    uint32_t off = 0;
    while (off < len) {
        uint32_t space = FLASH_SECTOR_SIZE - g_ota_sector_used;
        uint32_t take = len - off;
        if (take > space) take = space;
        std::memcpy(g_ota_sector + g_ota_sector_used, data + off, take);
        g_ota_sector_used += take;
        g_ota_received += take;
        off += take;
        if (g_ota_sector_used == FLASH_SECTOR_SIZE && !ota_flush_sector()) {
            ota_upload_fail(tpcb, "Error de escritura en Flash");
            return false;
        }
    }
    g_ota_crc = ota_crc32(g_ota_crc, data, len);
    return true;
}

// Finaliza la subida: verifica el CRC y, si es correcto, escribe los metadatos
// de commit y pide el reinicio. Si falla, NO conmuta (sigue la imagen actual).
static void ota_finish(struct tcp_pcb* tpcb) {
    if (!ota_flush_sector()) {
        ota_upload_fail(tpcb, "Error de escritura en Flash");
        return;
    }

    bool ok = g_ota_have_expected_crc &&
              (g_ota_crc == g_ota_expected_crc) &&
              (g_ota_received == g_ota_content_length) &&
              (g_ota_received > 0) &&
              (g_ota_received <= OTA_SLOT_SIZE);

    if (!ok) {
        ota_upload_reset();
        send_http_response(tpcb, "422 Unprocessable Content", "text/plain",
                           "CRC incorrecto: actualizacion rechazada");
        return;
    }

    // Prepara los metadatos de commit (conserva app_len/app_crc32 si existen).
    ota_metadata_t meta;
    ota_meta_read(&meta);
    if (!ota_meta_valid(&meta)) {
        std::memset(&meta, 0, sizeof(meta));
        meta.magic = OTA_METADATA_MAGIC;
        meta.version = OTA_METADATA_VERSION;
    }
    meta.commit_state = OTA_COMMIT_PENDING;
    meta.staging_len = g_ota_received;
    meta.staging_crc32 = g_ota_crc;
    ota_meta_finalize(&meta);

    if (!flash::ota_write_metadata(&meta)) {
        ota_upload_reset();
        send_http_response(tpcb, "500 Internal Server Error", "text/plain",
                           "Error al escribir los metadatos");
        return;
    }

    ota_upload_reset();
    send_http_response(tpcb, "200 OK", "text/plain",
                       "Actualizacion recibida y verificada. Reiniciando...");
    g_ota_reboot_pending = true;
}

// Procesa un segmento TCP de una subida OTA (cabeceras + cuerpo).
static bool ota_handle_body(struct tcp_pcb* tpcb, struct pbuf* p) {
    struct pbuf* q = p;
    while (q) {
        const uint8_t* data = (const uint8_t*)q->payload;
        u16_t len = q->len;

        if (!g_ota_headers_done) {
            bool done = false;
            for (u16_t i = 0; i < len && !done; i++) {
                if (g_ota_header_len >= sizeof(g_ota_header) - 1u) {
                    ota_upload_fail(tpcb, "Cabecera demasiado grande");
                    return false;
                }
                g_ota_header[g_ota_header_len++] = (char)data[i];
                g_ota_header[g_ota_header_len] = '\0';
                if (g_ota_header_len >= 4 &&
                    std::memcmp(g_ota_header + g_ota_header_len - 4, "\r\n\r\n", 4) == 0) {
                    const char* cl = find_header(g_ota_header, "Content-Length");
                    g_ota_content_length = cl ? (uint32_t)std::strtoul(skip_ws(cl), nullptr, 10) : 0;
                    const char* crc = find_header(g_ota_header, "X-CRC32");
                    if (crc) {
                        g_ota_expected_crc = (uint32_t)std::strtoul(skip_ws(crc), nullptr, 16);
                        g_ota_have_expected_crc = true;
                    }
                    g_ota_headers_done = true;
                    done = true;
                    if (i + 1u < len &&
                        !ota_stream_chunk(tpcb, data + i + 1u, len - (i + 1u))) {
                        return false;
                    }
                }
            }
        } else {
            if (!ota_stream_chunk(tpcb, data, len)) return false;
        }
        q = q->next;
    }
    return true;
}

// Comprueba si la petición es un POST a /ota/upload (solo mira el primer pbuf).
static bool is_ota_upload_start(struct pbuf* p) {
    if (!p || p->len < 16) return false;
    const char* data = (const char*)p->payload;
    if (std::memcmp(data, "POST ", 5) != 0) return false;
    const char* start = data + 5;
    const char* end = start;
    while (end < data + p->len && *end != ' ' && *end != '?' && *end != '\r' && *end != '\n') end++;
    return ((size_t)(end - start) == 11 &&
            std::memcmp(start, "/ota/upload", 11) == 0);
}

// Se llama al recibir datos del cliente.
err_t http_recv(void*, struct tcp_pcb* tpcb, struct pbuf* p, err_t err) {
    if (p == nullptr) {
        // El cliente cerró la conexión (o abortó una subida OTA).
        if (g_ota_pcb == tpcb) ota_upload_reset();
        if (tpcb) tcp_close(tpcb);
        return ERR_OK;
    }

    // --- OTA: subida de firmware en streaming (POST /ota/upload). ---
    if (g_ota_pcb == tpcb || is_ota_upload_start(p)) {
        if (g_ota_pcb != tpcb) {
            ota_upload_reset();
            g_ota_pcb = tpcb;
        }
        if (ota_handle_body(tpcb, p)) {
            tcp_recved(tpcb, p->tot_len);
            if (g_ota_headers_done && g_ota_content_length > 0 &&
                g_ota_received >= g_ota_content_length) {
                ota_finish(tpcb);
            }
        }
        pbuf_free(p);
        return ERR_OK;
    }

    // Copia la petición a un buffer estático (solo interesa la primera línea).
    // Estático (no en stack) para no presionar el stack del Core 1, y de 1024 B
    // para admitir SSID + contraseña largas URL-encoded (~300 B) sin truncar.
    static char request[1024];
    size_t len = p->tot_len < sizeof(request) - 1u ? p->tot_len : sizeof(request) - 1u;
    pbuf_copy_partial(p, request, (u16_t)len, 0);
    request[len] = '\0';
    pbuf_free(p);

    // Nota de diseño: se usa un único buffer global g_body para la respuesta.
    // En NO_SYS=0 las callbacks de lwIP están serializadas, así que no hay
    // concurrencia real entre conexiones en este servidor de un solo hilo.

    // Extrae la ruta (entre "GET " y el siguiente espacio).
    static char path[1024] = "/";
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

    // --- Rate limiting global (ventana fija): protege todas las rutas. ---
    if (rate_limited(&g_rate_window_start, &g_rate_count, RATE_WINDOW_MS, RATE_MAX_REQS)) {
        send_http_response(tpcb, "429 Too Many Requests", "text/plain",
                           "429 Too Many Requests");
        return ERR_OK;
    }

    // --- Captive portal check (conectividad) ---
    // Los teléfonos/PC hacen un GET a una URL conocida para detectar "internet".
    // Si no reciben lo esperado, se desconectan de la red "sin internet". Se
    // responde lo que cada plataforma espera para que mantengan la conexión.
    if (std::strstr(path, "generate_204") || std::strstr(path, "gen_204")) {
        send_http_response(tpcb, "204 No Content", nullptr, nullptr);          // Android.
        return ERR_OK;
    }
    if (std::strstr(path, "hotspot-detect") || std::strstr(path, "success.html")) {
        send_http_response(tpcb, "200 OK", "text/plain", "Success");           // iOS.
        return ERR_OK;
    }
    if (std::strstr(path, "connecttest") || std::strstr(path, "ncsi") || std::strstr(path, "redirect")) {
        send_http_response(tpcb, "200 OK", "text/plain", "Microsoft Connect Test"); // Windows.
        return ERR_OK;
    }
    // Favicon y otros recursos estáticos: 404 rápido (evita que el navegador
    // espere o pida la página completa como si fuera el favicon).
    if (std::strncmp(path, "/favicon.ico", 12) == 0) {
        send_http_response(tpcb, "404 Not Found", nullptr, nullptr);
        return ERR_OK;
    }

    // --- Escaneo de redes Wi-Fi: devuelve el caché actual como JSON y pide ---
    // --- un escaneo fresco al bucle principal (no bloquea en este contexto). ---
    if (std::strncmp(path, "/scan", 5) == 0) {
        g_scan_requested = true;   // El bucle principal ejecuta wifi_scan_now().
        int r = 0;
        r += std::snprintf(g_body + r, sizeof(g_body) - r, "[");
        for (int i = 0; i < g_scan_count && r < (int)sizeof(g_body) - 8; i++) {
            r += std::snprintf(g_body + r, sizeof(g_body) - r, "%s\"", (i ? "," : ""));
            for (const char* s = g_scan_ssids[i]; *s && r < (int)sizeof(g_body) - 8; s++) {
                if (*s == '"' || *s == '\\') { g_body[r++] = '\\'; }
                g_body[r++] = *s;
            }
            g_body[r++] = '"';
        }
        std::snprintf(g_body + r, sizeof(g_body) - r, "]");
        send_http_response(tpcb, "200 OK", "application/json", g_body);
        return ERR_OK;
    }

    // --- Autenticación PIN para rutas protegidas (control + configuración). ---
    // Sin cookie/token válido -> 401: los verbos de control se rechazan.
    if (is_protected_path(path) && !is_authorized(request)) {
        send_http_response(tpcb, "401 Unauthorized", "text/plain",
                           "401 Unauthorized",
                           "WWW-Authenticate: Bearer realm=\"PicoReflex\"\r\n");
        return ERR_OK;
    }

    // --- Sesión PIN: /login valida el PIN y setea la cookie; /logout la borra. ---
    if (std::strncmp(path, "/login", 6) == 0) {
        const char* pin = find_param(query, "pin");
        if (pin && *pin) {
            // Solo un intento de login real (con ?pin=) consume el limitador anti
            // fuerza bruta. Un GET plano de /login cae al render del formulario.
            if (rate_limited(&g_login_window_start, &g_login_count,
                             LOGIN_WINDOW_MS, LOGIN_MAX_REQS)) {
                send_http_response(tpcb, "429 Too Many Requests", "text/plain",
                                   "429 Too Many Requests");
                return ERR_OK;
            }
            if (pin_equal(pin, WEB_PIN)) {
                // Éxito: setea la cookie de sesión y redirige al Home (302) para no
                // dejar el PIN en la URL/historial del navegador.
                char setcookie[128];
                std::snprintf(setcookie, sizeof(setcookie),
                              "Set-Cookie: pr_auth=%s; Path=/; HttpOnly\r\n", auth_token());
                send_redirect(tpcb, "/", setcookie);
            } else {
                send_http_response(tpcb, "401 Unauthorized", "text/plain", "PIN incorrecto");
            }
            return ERR_OK;
        }
        // sin ?pin= : cae al render de abajo (render_login muestra el formulario).
    }
    if (std::strncmp(path, "/logout", 7) == 0) {
        // Borra la cookie (Max-Age=0) y vuelve al Home.
        send_redirect(tpcb, "/", "Set-Cookie: pr_auth=; Path=/; Max-Age=0\r\n");
        return ERR_OK;
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
    // Se ejecuta desde network_task (no desde el hilo tcpip): la API raw de
    // lwIP (tcp_new/bind/listen/accept) requiere el lock del core.
    cyw43_arch_lwip_begin();
    struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) { cyw43_arch_lwip_end(); return; }
    if (tcp_bind(pcb, IP_ADDR_ANY, HTTP_PORT) != ERR_OK) { tcp_close(pcb); cyw43_arch_lwip_end(); return; }
    struct tcp_pcb* l = tcp_listen(pcb);
    if (!l) { tcp_close(pcb); cyw43_arch_lwip_end(); return; }
    tcp_accept(l, http_accept);
    cyw43_arch_lwip_end();
}

} // namespace

// =============================================================================
// Inicialización y bucle de la tarea de red
// =============================================================================
void init() {
    // Inicializa el chip Wi-Fi + lwIP (sys_freertos crea la tarea tcpip). Se hace
    // DENTRO de la tarea de red (patrón oficial pico-examples), UNA sola vez.
    printf("[web] cyw43_arch_init()...\n");
    if (cyw43_arch_init()) {
        printf("[web] fallo cyw43_arch_init\n");
        return;
    }
    printf("[web] cyw43_arch_init OK\n");
    // Medición de batería (ADC, divisor externo en GPIO26). Se hace en la tarea de
    // red porque es quien publica el estado (batería) para la web y el display.
    battery::init();
}

// Apaga los servicios del AP (DNS/DHCP). Con FreeRTOS NO se hace cyw43_arch_deinit:
// el chip se inicializa UNA vez en init(), dentro de la tarea de red.
static void network_down() {
    captive::stop();        // Libera el servidor DNS (captive portal) si estaba.
    ap_dhcp_stop();         // Detiene el servidor DHCP del AP.
}

// -----------------------------------------------------------------------------
// mDNS: publica picoreflex.local (solo en modo STA)
// -----------------------------------------------------------------------------
// Registra el netif STA en el responder mDNS de lwIP (hostname picoreflex.local)
// y anuncia el servicio HTTP (_http) en el puerto 80. En modo AP no se registra
// (el cliente accede por http://192.168.4.1). Las llamadas a la API raw de lwIP
// se protegen con cyw43_arch_lwip_begin/end, que en el SDK de la Pico equivale
// al lock del núcleo lwIP (PICO_LWIP_CUSTOM_LOCK_TCPIP_CORE -> async_context).

// Inicializa el responder mDNS (socket UDP 5353) una sola vez.
static void mdns_init_once() {
    if (g_mdns_inited) return;
    cyw43_arch_lwip_begin();
    mdns_resp_init();
    cyw43_arch_lwip_end();
    g_mdns_inited = true;
}

// Registra picoreflex.local + servicio HTTP en el netif STA. En reconexiones
// re-anuncia (announce) en vez de volver a añadir: evita el "Double add" de
// mdns_resp_add_netif (el hostname ya persiste en el netif).
static void mdns_register_sta() {
    mdns_init_once();
    struct netif* nif = &cyw43_state.netif[CYW43_ITF_STA];
    cyw43_arch_lwip_begin();
    if (g_mdns_sta_registered) {
        mdns_resp_announce(nif);
    } else {
        err_t err = mdns_resp_add_netif(nif, MDNS_HOSTNAME);
        if (err == ERR_OK) {
            mdns_resp_add_service(nif, MDNS_SERVICE_NAME, MDNS_SERVICE_TYPE,
                                  DNSSD_PROTO_TCP, HTTP_PORT, nullptr, nullptr);
            g_mdns_sta_registered = true;
            printf("[web] mDNS: %s.local (HTTP :%u)\n", MDNS_HOSTNAME, HTTP_PORT);
        } else {
            printf("[web] mDNS: add_netif falló (err=%d)\n", (int)err);
        }
    }
    cyw43_arch_lwip_end();
}

// Retira el registro mDNS (al degradar a AP o apagar la red STA).
static void mdns_unregister_sta() {
    if (!g_mdns_sta_registered) return;
    cyw43_arch_lwip_begin();
    mdns_resp_remove_netif(&cyw43_state.netif[CYW43_ITF_STA]);
    cyw43_arch_lwip_end();
    g_mdns_sta_registered = false;
}

// -----------------------------------------------------------------------------
// Máquina de estados de red: STA (con reintentos) -> SoftAP (degradación)
// -----------------------------------------------------------------------------

// Publica la IP actual de la interfaz STA en el estado compartido (web + display).
static void sta_refresh_ip() {
    uint32_t a = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;
    char ip[16];
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                  a & 0xffu, (a >> 8) & 0xffu, (a >> 16) & 0xffu, (a >> 24) & 0xffu);
    system_status_update_network(true, false, ip);
    printf("[web] STA activa, IP %s\n", ip);
}

// Intenta (re)establecer el enlace STA. Bloquea hasta STA_TIMEOUT_MS (10 s).
// Devuelve true si conectó. NO degrada a AP: lo decide el llamador.
static bool sta_start() {
    GameConfig cfg = config_get();
    cyw43_arch_enable_sta_mode();   // No-op si STA ya estaba activo.
    printf("[web] Intentando STA: %s ...\n", cfg.wifi_ssid);
    bool ok = (cyw43_arch_wifi_connect_timeout_ms(cfg.wifi_ssid, cfg.wifi_password,
                                                   CYW43_AUTH_WPA2_AES_PSK,
                                                   STA_TIMEOUT_MS) == 0);
    if (ok) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);  // LED fijo = STA.
        sta_refresh_ip();
        mdns_register_sta();   // Publica picoreflex.local en el enlace STA.
    }
    return ok;
}

// Habilita el SoftAP de respaldo (captive portal + DHCP) y publica el estado.
static void ap_start() {
    g_is_ap = true;
    mdns_unregister_sta();   // En AP no hay mDNS; retira picoreflex.local.
    cyw43_arch_disable_sta_mode();
    cyw43_wifi_ap_set_channel(&cyw43_state, AP_CHANNEL);   // Canal fijo (estable).

    // Clave del AP: la configurada en /wifi (persistida) o, si está vacía, la
    // clave por defecto (AP_PASSWORD_DEFAULT).
    GameConfig cfg = config_get();
    if (cfg.ap_password[0] != '\0') {
        std::strncpy(g_ap_password, cfg.ap_password, sizeof(g_ap_password) - 1u);
        g_ap_password[sizeof(g_ap_password) - 1u] = '\0';
    } else {
        flash::ap_password_default(g_ap_password, sizeof(g_ap_password));
    }

#if AP_OPEN
    cyw43_arch_enable_ap_mode(AP_SSID, nullptr, CYW43_AUTH_OPEN);
    printf("[web] Modo AP (ABIERTO): %s / canal %d / IP %s\n", AP_SSID, AP_CHANNEL, AP_IP);
#else
    cyw43_arch_enable_ap_mode(AP_SSID, g_ap_password, CYW43_AUTH_WPA2_MIXED_PSK);
    printf("[web] Modo AP: %s / clave %s / IP %s\n", AP_SSID, g_ap_password, AP_IP);
#endif
    system_status_update_network(false, true, AP_IP);
    captive::init();    // DNS en puerto 53 -> 192.168.4.1 (redirección).
    ap_dhcp_start();    // DHCP: asigna IP 192.168.4.16..23 a los clientes.
}

// Configura la red (STA -> SoftAP), el captive portal (si AP) y el servidor HTTP.
// NO llama a cyw43_arch_init(): ya se hizo en init() (sys_freertos), dentro de la
// tarea de red. Se llama al arrancar y de nuevo en cada reconexión (cambio de
// credenciales).
static void network_up() {
    g_is_ap = false;
    // Reinicia la máquina de estados de re-conexión (oportunidad completa nueva).
    g_sta_retries = 0;
    g_sta_backoff_ms = STA_BACKOFF_BASE_MS;
    g_sta_backoff_until = 0;

    // Diagnóstico por USB (stdio). Si el usuario mantiene el botón 0 al arrancar,
    // se fuerza AP sin intentar STA.
    if (!g_force_ap && sta_start()) {
        http_server_init();
        return;
    }
    if (g_force_ap) {
        printf("[web] STA omitido (forzar AP)\n");
    }
    ap_start();
    http_server_init();
}

// Vigilancia del enlace STA en caliente (máquina de estados STA -> AP).
//   - Enlace sano (CYW43_LINK_JOIN): reinicia el contador y publica la IP real.
//   - Enlace caído: reintenta con backoff exponencial.
//   - Tras STA_MAX_RETRIES fallos consecutivos: degrada a SoftAP.
static void sta_link_watchdog(uint32_t now_ms) {
    if (g_is_ap) return;   // Ya degradado; no reintenta STA por sí solo.

    int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link == CYW43_LINK_JOIN) {
        // Enlace sano: reinicia el contador y publica la IP real (por si cambió).
        if (g_sta_retries != 0) {
            g_sta_retries = 0;
            g_sta_backoff_ms = STA_BACKOFF_BASE_MS;
            printf("[web] Enlace STA restablecido\n");
        }
        g_sta_backoff_until = 0;
        sta_refresh_ip();
        return;
    }

    // Enlace caído. Si aún está en espera de backoff, no hace nada.
    if (g_sta_backoff_until != 0 && (int32_t)(now_ms - g_sta_backoff_until) < 0) {
        return;
    }

    if (g_sta_retries == 0 && g_sta_backoff_until == 0) {
        printf("[web] Enlace STA caído (link=%d), reintentando...\n", link);
    }

    if (g_sta_retries >= STA_MAX_RETRIES) {
        printf("[web] %u reintentos fallidos, degradando a SoftAP\n",
               (unsigned)STA_MAX_RETRIES);
        captive::stop();
        ap_dhcp_stop();
        ap_start();
        return;
    }

    // Reintenta (bloquea hasta 10 s). Al volver se RELEE el reloj para programar
    // el próximo backoff con el tiempo real (el `now_ms` de entrada ya quedó
    // obsoleto tras el bloqueo).
    if (sta_start()) {
        g_sta_retries = 0;
        g_sta_backoff_ms = STA_BACKOFF_BASE_MS;
        g_sta_backoff_until = 0;
        return;
    }

    g_sta_retries++;
    g_sta_backoff_until = (uint32_t)(time_us_32() / 1000u) + g_sta_backoff_ms;
    printf("[web] STA reintento %u fallido, próximo en %u ms\n",
           (unsigned)g_sta_retries, (unsigned)g_sta_backoff_ms);
    g_sta_backoff_ms *= 2u;
    if (g_sta_backoff_ms > STA_BACKOFF_MAX_MS) {
        g_sta_backoff_ms = STA_BACKOFF_MAX_MS;
    }
}

void run() {
    // Levanta la red la primera vez.
    network_up();
    bool net_up = true;

    // --- Bucle de la tarea de red (display + LED indicador + batería + dormancia). ---
    uint32_t blink_last = 0;
    uint32_t batt_last = 0;
    bool blink_state = false;
    while (true) {
        // OTA completada: reinicia para que el bootloader haga el commit/rollback.
        if (g_ota_reboot_pending) {
            vTaskDelay(pdMS_TO_TICKS(500));   // Da tiempo a enviar la respuesta HTTP.
            watchdog_reboot(0, 0, 100);       // Reinicia por hardware watchdog.
        }

        watchdog_update();   // Alimenta el watchdog del sistema desde el Core 1.

        // Protocolo de DORMANT (degradado con FreeRTOS): el Core 0 pide "dormir",
        // solo se detienen los servicios del AP. No se entra en DORMANT real.
        if (g_sleep_request) {
            if (net_up) {
                network_down();
                net_up = false;
                g_sleep_ready = true;   // Avisa al Core 0 que ya puede dormir.
            }
            vTaskDelay(pdMS_TO_TICKS(50));
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
            // Espera a que el Core 0 termine de persistir en Flash antes de tocar
            // el CYW43 (evita la carrera Flash vs cyw43).
            uint32_t wait_start = (uint32_t)(time_us_32() / 1000u);
            while (!g_persist_done &&
                   ((uint32_t)(time_us_32() / 1000u) - wait_start) < 2000u) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            g_wifi_reconnect = false;
            // Re-conexión "liviana": apaga el modo actual y re-ejecuta STA/AP.
            // (NO se hace cyw43_arch_deinit/init con FreeRTOS.)
            captive::stop();
            ap_dhcp_stop();
            if (g_is_ap) {
                cyw43_arch_disable_ap_mode();
            } else {
                cyw43_arch_disable_sta_mode();
            }
            network_up();
            net_up = true;
            blink_last = (uint32_t)(time_us_32() / 1000u);
            batt_last  = blink_last;
        }

        // Escaneo de redes Wi-Fi solicitado desde /scan (autocompletar SSID).
        if (g_scan_requested) {
            g_scan_requested = false;
            if (net_up) {
                wifi_scan_now();
            }
        }

        uint32_t now_ms = (uint32_t)(time_us_32() / 1000u);

        // Vigilancia del enlace STA: reconexión en caliente (backoff -> SoftAP).
        if (now_ms - g_link_last_check >= STA_LINK_CHECK_MS) {
            sta_link_watchdog(now_ms);
            g_link_last_check = (uint32_t)(time_us_32() / 1000u);
        }

        // Parpadeo del LED onboard en modo AP.
        if (g_is_ap) {
            if (now_ms - blink_last > 250u) {
                blink_last = now_ms;
                blink_state = !blink_state;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, blink_state);
            }
        }

        // Muestrea la batería ~1 vez por segundo y publica en estado compartido.
        // Se re-aplica la calibración desde la config (persistida/editable en la
        // web) antes de cada lectura, para reflejar cambios en caliente.
        if (now_ms - batt_last > 1000u) {
            batt_last = now_ms;
            battery::set_calibration(config_get().batt_calib);
            uint32_t mv  = battery::read_voltage_mv();
            uint8_t  pct = battery::voltage_to_percent(mv);
            system_status_update_battery(mv, pct);
        }

        refresh_display();
        vTaskDelay(pdMS_TO_TICKS(200));   // Refresco del display ~5 veces por segundo.
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

    // Selecciona el controlador desde la config (persistido/editable en /display)
    // y re-inicializa el panel si el chip cambió en runtime.
    display::Controller ctrl = (cfg.display_controller == 1)
                                   ? display::Controller::SH1106
                                   : display::Controller::SSD1306;
    static display::Controller last_ctrl = display::Controller::SSD1306;
    static bool initialized = false;
    if (!initialized || ctrl != last_ctrl) {
        initialized = display::init(ctrl);
        last_ctrl = ctrl;
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
        // En AP, muestra la clave en pantalla (la IP es siempre 192.168.4.1) para
        // que el usuario pueda conectarse sin acceso por USB.
        display::draw_printf(0, 7, "PASS:%s", g_ap_password);
    } else if (sys.wifi_sta) {
        display::draw_string(0, 6, "WIFI:STA");
        display::draw_string(0, 7, sys.ip);
    } else {
        display::draw_string(0, 6, "WIFI:--");
        display::draw_string(0, 7, sys.ip);
    }

    display::show();
}

} // namespace web
