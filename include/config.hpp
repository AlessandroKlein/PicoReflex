/**
 * @file   config.hpp
 * @brief  Tipos de datos y configuración compartida entre núcleos del PicoReflex.
 *
 * Este header define:
 *  - Los modos de juego y niveles de dificultad.
 *  - La estructura de configuración `GameConfig` (modificable desde el servidor web).
 *  - La estructura de estadísticas `GameStats` (actualizada por el núcleo 0 y
 *    leída por el núcleo 1 para el panel web y el display).
 *  - La API de acceso con exclusión mutua (critical_section) para compartir datos
 *    entre el Core 0 (juego) y el Core 1 (red/display) de forma segura.
 *
 * Decisión de diseño (multinúcleo): el SDK recomienda (pico/multicore.h) no usar
 * los FIFOs inter-núcleo para datos propios y usar en su lugar `pico/util/queue.h`
 * o estructuras protegidas. Aquí usamos:
 *   - critical_section_t para proteger el estado compartido (accesos muy cortos).
 *   - queue_t (pico/util/queue.h) para los eventos asíncronos Core1 -> Core0
 *     (cambio de configuración solicitado desde la web).
 *
 * Referencias:
 *  - https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_multicore
 *  - https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#group_queue
 */

#ifndef PICO_REFLEX_CONFIG_HPP
#define PICO_REFLEX_CONFIG_HPP

#include "pico/stdlib.h"
#include "pico/sync.h"          // critical_section_t, mutex_t
#include "pico/util/queue.h"    // queue_t (eventos inter-núcleo)

#include "pin_defs.hpp"

// =============================================================================
// Tipo de LEDs (selección en tiempo de compilación)
// =============================================================================
// 0 = LEDs GPIO (por defecto): 5 LEDs con resistencia en GPIO2..6, encendido
//     simple (HIGH = encendido).
// 1 = WS2812 (NeoPixel): tira/diodos direccionables RGB en un único pin de datos
//     (GPIO28), controlados por PIO. Permite efectos de color al ganar/perder.
// Se deja la posibilidad de ambos tipos: cambiando esta macro y el cableado.
#ifndef PICOREFLEX_WS2812
#define PICOREFLEX_WS2812 0
#endif

// =============================================================================
// Controlador del display OLED (selección en tiempo de compilación)
// =============================================================================
// 0 = SSD1306 128x64 (por defecto): sin offset de columna.
// 1 = SH1106 132x64: el área visible de 128 px empieza en la columna 2, por lo
//     que el driver aplica un offset de columna +2 (0x21, 0x02, 0x7F) y la
//     configuración de pines COM 0x02 (0xDA, 0x02).
#ifndef PICOREFLEX_DISPLAY_CONTROLLER
#define PICOREFLEX_DISPLAY_CONTROLLER 0
#endif

// =============================================================================
// Credenciales Wi-Fi por defecto (compilación)
// =============================================================================
// Se usan para inicializar GameConfig; pueden cambiarse desde la web y persisten
// en Flash. Si se modifica una red, se re-conecta automáticamente.
#ifndef WIFI_SSID_DEFAULT
#define WIFI_SSID_DEFAULT     ""
#endif
#ifndef WIFI_PASSWORD_DEFAULT
#define WIFI_PASSWORD_DEFAULT ""
#endif

// =============================================================================
// Modos de juego
// =============================================================================
enum class GameMode : uint8_t {
    // Entrenamiento libre: sin penalización por tiempo; solo cuenta aciertos/fallos.
    FREE_PLAY = 0,
    // Contrarreloj: ronda de duración fija; se acumulan aciertos hasta agotar el
    // tiempo. Un fallo o un timeout cuentan en contra pero no terminan la ronda.
    TIMED = 1,
    // Muerte súbita: un único fallo (o timeout) termina la ronda de inmediato.
    SUDDEN_DEATH = 2,
    // Simón Dice: secuencia de LEDs que crece. El jugador reproduce la secuencia;
    // al acertar se agrega un elemento y se repite; al fallar, Game Over y reinicio
    // a longitud 1. La secuencia es determinista según la semilla.
    SIMON_SAYS = 3,
    MODE_COUNT = 4
};

// =============================================================================
// Niveles de dificultad (tolerancia máxima de reacción)
// =============================================================================
enum class Difficulty : uint8_t {
    EASY   = 0,  ///< Tolerancia 1000 ms.
    NORMAL = 1,  ///< Tolerancia  500 ms.
    HARD   = 2,  ///< Tolerancia  250 ms.
    DIFF_COUNT = 3
};

// =============================================================================
// Calibración de la batería (curva de descarga + compensación de carga)
// =============================================================================
// Curva de descarga LiPo/Li-Ion de 1 celda definida por 3 puntos (tabla por
// tramos con interpolación lineal): tensión a 100 %, 50 % y 0 % de carga.
// `load_comp_mv` es una corrección por corriente estimada (caída resistiva /
// IR drop) que se suma a la tensión medida cuando el consumo (Wi-Fi) está activo.
struct BatteryCalibration {
    uint32_t v_full_mv   = 4200;   ///< Tensión (mV) con 100 % de carga.
    uint32_t v_mid_mv    = 3700;   ///< Tensión (mV) con 50 % de carga (inflexión).
    uint32_t v_empty_mv  = 3000;   ///< Tensión (mV) con 0 % de carga (corte).
    uint32_t load_comp_mv = 0;     ///< Compensación por carga (mV) sumada a la lectura.
};

// =============================================================================
// Configuración del juego (modificable en tiempo real vía web)
// =============================================================================
struct GameConfig {
    // Semilla del PRNG. Si == 0, se usa una semilla aleatoria (get_rand_32()).
    // Si != 0, la secuencia de LEDs es determinista y repetible.
    uint32_t seed = 0;

    // Dificultad actual (define la tolerancia máxima en ms).
    Difficulty difficulty = Difficulty::NORMAL;

    // Modo de juego actual.
    GameMode mode = GameMode::FREE_PLAY;

    // Duración de la ronda (solo relevante en modo TIMED), en segundos.
    uint32_t round_seconds = 30;

    // Tiempo base entre estímulos (apagado de un LED y encendido del siguiente),
    // en milisegundos. El valor real se aleatoriza alrededor de este valor.
    uint32_t stimulus_min_ms = 500;   ///< Mínimo entre estímulos.
    uint32_t stimulus_max_ms = 1500;  ///< Máximo entre estímulos.

    // Tolerancia máxima (en ms) según la dificultad.
    // Se calcula con get_tolerance_ms(), no se guarda directo.

    // Nombre del jugador (persistido en Flash con los récords).
    char user_name[16] = "JUGADOR";

    // Retardo de cuenta regresiva (segundos) entre el disparador de inicio
    // (dos botones simultáneos) y el comienzo real de la ronda.
    uint32_t start_delay_s = 3;

    // Habilitar/deshabilitar el display OLED. Toda la información también se
    // muestra en la web, así que se puede apagar para ahorrar energía.
    bool oled_enabled = true;

    // Credenciales Wi-Fi (configurables desde la web; persistidas en Flash).
    char wifi_ssid[33]     = WIFI_SSID_DEFAULT;
    char wifi_password[65] = WIFI_PASSWORD_DEFAULT;

    // Contraseña del Access Point de respaldo (modo AP). Vacía = se deriva del
    // identificador único de la Flash (RUID), por lo que no hay clave por defecto
    // conocida. Configurable desde /wifi y persistida en Flash (8..15 caracteres).
    char ap_password[16]   = "";

    // Calibración de la curva de descarga de batería (configurable desde /battery
    // y persistida en Flash).
    BatteryCalibration batt_calib;
};

// =============================================================================
// Estadísticas del juego (actualizadas por el núcleo 0)
// =============================================================================
// Entrada del historial de reacción (para la tabla de la web): guarda el tiempo,
// la semilla usada y la dificultad de esa reacción.
struct HistoryEntry {
    uint32_t   time_ms;      ///< Tiempo de reacción en ms.
    uint32_t   seed;         ///< Semilla del PRNG en ese momento.
    Difficulty difficulty;   ///< Dificultad en ese momento.
};

struct GameStats {
    uint32_t total_rounds = 0;        ///< Total de estímulos presentados.
    uint32_t hits = 0;                ///< Aciertos (botón correcto dentro de la tolerancia).
    uint32_t misses = 0;              ///< Fallos (botón incorrecto).
    uint32_t timeouts = 0;            ///< Timeouts (no respondió a tiempo).
    uint32_t best_time_us = UINT32_MAX; ///< Mejor tiempo de reacción (microsegundos).
    uint32_t worst_time_us = 0;       ///< Peor tiempo de reacción (microsegundos).
    uint64_t total_time_us = 0;       ///< Suma de tiempos de reacción válidos.
    uint32_t valid_reactions = 0;     ///< Cantidad de reacciones válidas (para el promedio).
    bool round_active = false;        ///< true si hay una ronda en curso.

    // Historial de reacciones para la tabla de la web.
    // history[0] es el más reciente; time_ms == 0 = entrada vacía.
    static constexpr uint32_t HISTORY_SIZE = 20;
    HistoryEntry history[HISTORY_SIZE];
    uint32_t history_count = 0;       ///< Entradas válidas (0..HISTORY_SIZE).
};

// =============================================================================
// Eventos de control enviados del Core 1 (web) al Core 0 (juego)
// =============================================================================
enum class ControlEvent : uint8_t {
    NONE = 0,
    START_ROUND = 1,    ///< Comenzar una ronda nueva.
    STOP_ROUND = 2,     ///< Detener la ronda actual.
    RESET_STATS = 3,    ///< Reiniciar las estadísticas.
    CONFIG_CHANGED = 4, ///< Se modificó GameConfig (recargar desde estado compartido).
};

// =============================================================================
// Estado del sistema (batería + red) — compartido entre núcleos
// =============================================================================
struct SystemStatus {
    uint32_t battery_mv  = 0;        ///< Tensión de batería (mV).
    uint8_t  battery_pct = 0;        ///< Porcentaje de carga (0..100).
    bool     wifi_sta    = false;    ///< true si conectado como Station.
    bool     wifi_ap     = false;    ///< true si está en modo Access Point.
    char     ip[16]      = "0.0.0.0"; ///< Dirección IP actual (texto).
};

// =============================================================================
// Protocolo de dormancia (flags compartidos entre núcleos)
// =============================================================================
// El Core 0 (juego) pide dormir cuando hay 3 min de inactividad; el Core 1 (red)
// apaga el Wi-Fi (cyw43_arch_deinit) y responde; luego el Core 0 entra en modo
// DORMANT (< 2 mA). Al despertar por un pulsador, ambos núcleos reanudan.
// Son flags `volatile bool` (accesos de 1 byte, atómicos en ARM).
extern volatile bool g_sleep_request;   ///< Core 0 -> Core 1: "apagá el Wi-Fi".
extern volatile bool g_sleep_ready;     ///< Core 1 -> Core 0: "Wi-Fi apagado".
extern volatile bool g_force_ap;        ///< true = saltar STA e ir directo a SoftAP.
extern volatile bool g_persist_done;    ///< true = la persistencia a Flash terminó.

// =============================================================================
// API de estado compartido (thread-safe entre núcleos)
// =============================================================================

// Inicializa la sección crítica y las colas. Debe llamarse UNA vez en main().
void shared_state_init();

// --- Configuración -----------------------------------------------------------
// Devuelve una COPIA segura de la configuración actual.
GameConfig config_get();
// Reemplaza la configuración (llamado por el núcleo 1 al recibir datos web).
void config_set(const GameConfig& cfg);

// --- Estadísticas ------------------------------------------------------------
// Devuelve una COPIA segura de las estadísticas actuales.
GameStats stats_get();
// Reemplaza las estadísticas (por ejemplo al reiniciar).
void stats_set(const GameStats& s);
// Reinicia las estadísticas a cero.
void stats_reset();

// Registra un resultado de reacción (llamado por el núcleo 0, juego).
//  - reaction_us: tiempo en microsegundos (0 si hubo timeout).
//  - hit: true si acertó el botón correcto dentro de la tolerancia.
//  - timeout: true si se agotó el tiempo sin responder.
//  - seed / difficulty: contexto de la reacción (para el historial de la web).
void stats_record(uint32_t reaction_us, bool hit, bool timeout,
                  uint32_t seed, Difficulty difficulty);

// --- Eventos de control (Core1 -> Core0) -------------------------------------
// Encola un evento de control para el núcleo 0 (no bloquea si la cola está llena).
void control_send(ControlEvent ev);
// Extrae un evento de control (llamado por el núcleo 0). Devuelve false si no hay.
bool control_try_pop(ControlEvent* ev);

// --- Estado del sistema (batería + red) --------------------------------------
// Devuelve una copia segura del estado actual del sistema.
SystemStatus system_status_get();
// Actualiza los datos de batería (llamado por el Core 0).
void system_status_update_battery(uint32_t mv, uint8_t pct);
// Actualiza los datos de red (llamado por el Core 1).
void system_status_update_network(bool sta, bool ap, const char* ip);

// --- Utilidades derivadas ----------------------------------------------------
// Devuelve la tolerancia máxima (ms) asociada a una dificultad.
inline uint32_t difficulty_tolerance_ms(Difficulty d) {
    switch (d) {
        case Difficulty::EASY:   return 1000u;
        case Difficulty::NORMAL: return 500u;
        case Difficulty::HARD:   return 250u;
        default:                 return 500u;
    }
}

// Devuelve el nombre legible de un modo (para el display / web).
const char* game_mode_name(GameMode m);
// Devuelve el nombre legible de una dificultad.
const char* difficulty_name(Difficulty d);

#endif // PICO_REFLEX_CONFIG_HPP
