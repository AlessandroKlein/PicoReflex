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
    MODE_COUNT = 3
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
};

// =============================================================================
// Estadísticas del juego (actualizadas por el núcleo 0)
// =============================================================================
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
void stats_record(uint32_t reaction_us, bool hit, bool timeout);

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
