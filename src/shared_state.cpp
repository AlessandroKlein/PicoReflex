/**
 * @file   shared_state.cpp
 * @brief  Implementación del estado compartido entre núcleos (config.hpp).
 *
 * Usa `critical_section_t` (pico/sync.h) para proteger el acceso a la
 * configuración y las estadísticas desde los dos núcleos, y `queue_t`
 * (pico/util/queue.h) para los eventos de control asíncronos del Core 1 -> Core 0.
 *
 * Por qué critical_section y no mutex:
 *  - Las secciones críticas son muy cortas (copiar/guardar una struct), así que
 *    el bloqueo por spin es más simple y determinista.
 *  - Documentación: https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_sync
 */

#include "config.hpp"

#include <cstring>

// -----------------------------------------------------------------------------
// Objetos de sincronización y almacenamiento
// -----------------------------------------------------------------------------
static critical_section_t g_cs;       ///< Protege config y stats.
static GameConfig        g_config;    ///< Configuración actual (compartida).
static GameStats         g_stats;     ///< Estadísticas actuales (compartidas).
static SystemStatus      g_status;    ///< Estado del sistema (batería/red).
static queue_t           g_ctrl_q;    ///< Cola de eventos de control (Core1->Core0).

// Flags del protocolo de dormancia (ver config.hpp).
volatile bool g_sleep_request = false;
volatile bool g_sleep_ready   = false;
volatile bool g_force_ap      = false;
volatile bool g_persist_done  = true;   // Inicia "listo" (sin persistencia pendiente).

// -----------------------------------------------------------------------------
// Inicialización
// -----------------------------------------------------------------------------
void shared_state_init() {
    critical_section_init(&g_cs);

    // Cola de eventos de control. 16 slots es más que suficiente: el juego
    // consume los eventos a alta velocidad y la web envía como mucho uno por
    // petición HTTP. Cada evento es 1 byte (ControlEvent).
    // Si la cola se llena, control_send() descarta el evento (no bloquea).
    queue_init(&g_ctrl_q, sizeof(uint8_t), 16);
}

// -----------------------------------------------------------------------------
// Configuración
// -----------------------------------------------------------------------------
GameConfig config_get() {
    GameConfig c;
    critical_section_enter_blocking(&g_cs);
    c = g_config;
    critical_section_exit(&g_cs);
    return c;
}

void config_set(const GameConfig& cfg) {
    critical_section_enter_blocking(&g_cs);
    g_config = cfg;
    critical_section_exit(&g_cs);
}

// -----------------------------------------------------------------------------
// Estadísticas
// -----------------------------------------------------------------------------
GameStats stats_get() {
    GameStats s;
    critical_section_enter_blocking(&g_cs);
    s = g_stats;
    critical_section_exit(&g_cs);
    return s;
}

void stats_set(const GameStats& s) {
    critical_section_enter_blocking(&g_cs);
    g_stats = s;
    critical_section_exit(&g_cs);
}

void stats_reset() {
    GameStats s{};                       // Inicializa todos los campos a 0/UINT32_MAX.
    s.best_time_us = UINT32_MAX;         // El "mejor" arranca en el máximo posible.
    stats_set(s);
}

void stats_record(uint32_t reaction_us, bool hit, bool timeout,
                  uint32_t seed, Difficulty difficulty) {
    critical_section_enter_blocking(&g_cs);
    g_stats.total_rounds++;
    if (timeout) {
        g_stats.timeouts++;
    } else if (hit) {
        g_stats.hits++;
        // Solo las reacciones válidas participan del promedio / récords.
        if (reaction_us < g_stats.best_time_us) g_stats.best_time_us = reaction_us;
        if (reaction_us > g_stats.worst_time_us) g_stats.worst_time_us = reaction_us;
        g_stats.total_time_us += reaction_us;
        g_stats.valid_reactions++;

        // Historial para la tabla de la web: el más reciente en history[0].
        for (uint32_t i = GameStats::HISTORY_SIZE - 1u; i > 0; i--) {
            g_stats.history[i] = g_stats.history[i - 1u];
        }
        g_stats.history[0].time_ms    = reaction_us / 1000u;
        g_stats.history[0].seed       = seed;
        g_stats.history[0].difficulty = difficulty;
        if (g_stats.history_count < GameStats::HISTORY_SIZE) {
            g_stats.history_count++;
        }
    } else {
        g_stats.misses++;
    }
    critical_section_exit(&g_cs);
}

// -----------------------------------------------------------------------------
// Eventos de control
// -----------------------------------------------------------------------------
void control_send(ControlEvent ev) {
    uint8_t b = static_cast<uint8_t>(ev);
    // queue_try_add no bloquea: si la cola está llena se descarta el evento.
    // Es aceptable: el usuario puede reintentar desde la web.
    queue_try_add(&g_ctrl_q, &b);
}

bool control_try_pop(ControlEvent* ev) {
    uint8_t b = 0;
    if (queue_try_remove(&g_ctrl_q, &b)) {
        *ev = static_cast<ControlEvent>(b);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Estado del sistema (batería + red)
// -----------------------------------------------------------------------------
SystemStatus system_status_get() {
    SystemStatus s;
    critical_section_enter_blocking(&g_cs);
    s = g_status;
    critical_section_exit(&g_cs);
    return s;
}

void system_status_update_battery(uint32_t mv, uint8_t pct) {
    critical_section_enter_blocking(&g_cs);
    g_status.battery_mv = mv;
    g_status.battery_pct = pct;
    critical_section_exit(&g_cs);
}

void system_status_update_network(bool sta, bool ap, const char* ip) {
    critical_section_enter_blocking(&g_cs);
    g_status.wifi_sta = sta;
    g_status.wifi_ap = ap;
    std::strncpy(g_status.ip, ip ? ip : "0.0.0.0", sizeof(g_status.ip) - 1u);
    g_status.ip[sizeof(g_status.ip) - 1u] = '\0';
    critical_section_exit(&g_cs);
}

// -----------------------------------------------------------------------------
// Nombres legibles
// -----------------------------------------------------------------------------
const char* game_mode_name(GameMode m) {
    switch (m) {
        case GameMode::FREE_PLAY:    return "LIBRE";
        case GameMode::TIMED:        return "CONTRA";
        case GameMode::SUDDEN_DEATH: return "MUERTE";
        case GameMode::SIMON_SAYS:   return "SIMON";
        default:                     return "?";
    }
}

const char* difficulty_name(Difficulty d) {
    switch (d) {
        case Difficulty::EASY:   return "FACIL";
        case Difficulty::NORMAL: return "NORMAL";
        case Difficulty::HARD:   return "DIFICIL";
        default:                 return "?";
    }
}
