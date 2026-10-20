/**
 * @file   game_logic.cpp
 * @brief  Motor del juego de reacción (Core 0).
 *
 * Responsabilidades:
 *  - Configurar LEDs (salidas) y pulsadores (entradas + IRQ por flanco de bajada).
 *  - Medir la reacción con resolución de microsegundos:
 *      * El timestamp lo captura la IRQ GPIO con time_us_32() (hardware_timer),
 *        dentro de la propia interrupción, para minimizar latencia/jitter.
 *      * La resta sin signo `t_final - t_inicio` es segura ante el "wrap" del
 *        contador de 32 bits (~71.6 min).
 *  - Usar DMA para mantener un "trace" hardware de los 5 pulsadores: un canal DMA
 *    con buffer en anillo, rítmado por un slice PWM, copia continuamente el
 *    registro de entrada SIO (sio_hw->gpio_in) SIN intervención de la CPU.
 *    (El timing crítico de reacción NO depende de este log; ver docs/DECISIONES.md)
 *  - Ejecutar la máquina de estados del juego y actualizar estadísticas.
 *  - Persistir récords en Flash (flash_storage) desde este núcleo.
 *
 * Referencias:
 *  - hardware_gpio / gpio_set_irq_enabled_with_callback:
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio
 *  - hardware_timer / time_us_32:
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_timer
 *  - hardware_dma / channel_config_set_ring / channel_config_set_dreq:
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_dma
 */

#include "game_logic.hpp"

#include <cstring>

#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"
#include "hardware/structs/sio.h"
#include "pico/time.h"
#include "pico/rand.h"

#include "audio.hpp"
#include "flash_storage.hpp"
#include "pin_defs.hpp"

namespace game {

// -----------------------------------------------------------------------------
// Pines
// -----------------------------------------------------------------------------
static const uint g_led_pins[LED_COUNT]    = LED_PINS;    // GPIO 2..6
static const uint g_button_pins[LED_COUNT] = BUTTON_PINS; // GPIO 7..11

// -----------------------------------------------------------------------------
// PRNG
// -----------------------------------------------------------------------------
static XorShift32 g_rng;

// -----------------------------------------------------------------------------
// Estado de la IRQ de botones (volatile: se escribe en IRQ, se lee en el bucle)
// -----------------------------------------------------------------------------
static volatile bool     g_button_pending = false; ///< true si hay una pulsación.
static volatile uint32_t g_button_ts_us   = 0;     ///< Timestamp (us) del flanco.
static volatile uint8_t  g_button_index   = 0;     ///< Índice de botón (0..4).

// Timestamp del último flanco por botón, para el debounce software.
static uint32_t g_last_edge_us[LED_COUNT] = {0};

// -----------------------------------------------------------------------------
// DMA: trace hardware de los 5 pulsadores (buffer en anillo)
// -----------------------------------------------------------------------------
#define BUTTON_LOG_SIZE 256u                          ///< Entradas del log.
static uint32_t g_button_log[BUTTON_LOG_SIZE] __attribute__((aligned(1024)));
static uint     g_dma_chan = 0;                       ///< Canal DMA del log.

// -----------------------------------------------------------------------------
// Estado de la ronda
// -----------------------------------------------------------------------------
// `volatile`: g_round_active se lee desde el Core 1 (display/web) a través de
// is_round_active(), así que debe leerse de memoria cada vez.
static volatile bool g_round_active = false;
static uint32_t g_round_start_us = 0;

// Datos persistentes (récords, nombre, semilla).
static flash::PersistentData g_persist;
static bool g_persist_dirty = false;   ///< true si hay que escribir a Flash.

// -----------------------------------------------------------------------------
// Modo de bajo consumo (idle)
// -----------------------------------------------------------------------------
// Si no hay actividad en IDLE_SLEEP_AFTER_US, el Core 0 entra en "sleep" (WFI)
// para reducir consumo. Un temporizador periódico lo despierta cada
// IDLE_WAKE_PERIOD_MS para seguir atendiendo eventos de la web (queue) y medir
// la inactividad. NOTA: el modo "dormant" (< 2 mA) requiere apagar el Wi-Fi y
// coordinar ambos núcleos; está documentado en docs/DUDAS.md.
#define IDLE_SLEEP_AFTER_US   (3u * 60u * 1000000u)  ///< 3 minutos.
#define IDLE_WAKE_PERIOD_MS   50u                    ///< Despertar periódico.

static uint32_t g_last_activity_us = 0;   ///< Última actividad (pulsación/ronda).
static struct repeating_timer g_wake_timer;

// Callback del temporizador de "despertar periódico" (no hace nada: solo
// interrumpe el WFI para que el bucle revise la cola de eventos).
static bool wake_timer_cb(struct repeating_timer*) {
    return true;   // true = seguir repitiendo.
}

// -----------------------------------------------------------------------------
// Callback de interrupción GPIO (pulsadores)
// -----------------------------------------------------------------------------
// Se dispara por FLANCO DE BAJADA (botón presionado = nivel bajo por el pull-up).
// Solo registra el timestamp y el índice; el procesamiento se hace en el bucle.
static void gpio_irq_callback(uint gpio, uint32_t events) {
    if (!(events & GPIO_IRQ_EDGE_FALL)) {
        return; // No nos interesan los flancos de subida.
    }

    // Traduce GPIO -> índice de botón.
    int idx = -1;
    for (uint i = 0; i < LED_COUNT; i++) {
        if (gpio == g_button_pins[i]) { idx = (int)i; break; }
    }
    if (idx < 0) return;

    uint32_t now = time_us_32();

    // Debounce: ignora rebotes dentro de la ventana BUTTON_DEBOUNCE_US.
    if ((now - g_last_edge_us[idx]) < BUTTON_DEBOUNCE_US) {
        return;
    }
    g_last_edge_us[idx] = now;

    g_button_index = (uint8_t)idx;
    g_button_ts_us = now;
    g_button_pending = true;
}

// -----------------------------------------------------------------------------
// LEDs
// -----------------------------------------------------------------------------
static void leds_off() {
    for (uint i = 0; i < LED_COUNT; i++) {
        gpio_put(g_led_pins[i], false);
    }
}

static void led_on(uint i) {
    gpio_put(g_led_pins[i], true);
}

// -----------------------------------------------------------------------------
// Inicialización
// -----------------------------------------------------------------------------
void init() {
    // --- LEDs: salidas digitales, apagados al inicio. ---
    for (uint i = 0; i < LED_COUNT; i++) {
        gpio_init(g_led_pins[i]);
        gpio_set_dir(g_led_pins[i], GPIO_OUT);
        gpio_put(g_led_pins[i], false);
    }

    // --- Pulsadores: entradas con pull-up + IRQ por flanco de bajada. ---
    for (uint i = 0; i < LED_COUNT; i++) {
        gpio_init(g_button_pins[i]);
        gpio_set_dir(g_button_pins[i], GPIO_IN);
        gpio_pull_up(g_button_pins[i]);
        gpio_set_irq_enabled_with_callback(g_button_pins[i], GPIO_IRQ_EDGE_FALL,
                                           true, &gpio_irq_callback);
    }

    // --- DMA: trace continuo de los botones. ---
    // Un slice PWM (sin pin de salida) actúa como "reloj" del DMA: cada vez que
    // el contador PWM hace wrap se dispara una transferencia. Así el DMA lee el
    // registro de entrada GPIO (sio_hw->gpio_in) a ritmo fijo y lo guarda en un
    // buffer en anillo, descargando a la CPU.
    g_dma_chan = dma_claim_unused_channel(true);

    const uint pace_slice = 7;   // Slice PWM 7 libre (el buzzer usa el slice 0).
    pwm_config pc = pwm_get_default_config();
    pwm_config_set_clkdiv(&pc, 1.0f);       // clk_sys / 1 = 125 MHz.
    pwm_config_set_wrap(&pc, 62500u);       // 125 MHz / 62500 = 2000 Hz.
    pwm_init(pace_slice, &pc, true);        // Arranca el contador (sin GPIO).

    dma_channel_config c = dma_channel_get_default_config(g_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);   // Lee siempre gpio_in.
    channel_config_set_write_increment(&c, true);   // Avanza por el buffer.
    channel_config_set_ring(&c, true, 10);          // Wrap del write addr (1024 B).
    channel_config_set_dreq(&c, DREQ_PWM_WRAP0 + pace_slice); // Ritmo por PWM.
    channel_config_set_chain_to(&c, g_dma_chan);    // Se reencadena a sí mismo.
    dma_channel_configure(g_dma_chan, &c, g_button_log, &sio_hw->gpio_in,
                          BUTTON_LOG_SIZE, true);

    // --- Carga de datos persistentes. ---
    if (!flash::load(&g_persist)) {
        flash::set_default(&g_persist);
    }

    // --- Temporizador periódico para despertar el Core 0 del WFI. ---
    g_last_activity_us = time_us_32();
    add_repeating_timer_ms(IDLE_WAKE_PERIOD_MS, wake_timer_cb, nullptr, &g_wake_timer);
}

// -----------------------------------------------------------------------------
// Persistencia
// -----------------------------------------------------------------------------
namespace {

// Marca los datos para guardar y ejecuta el guardado en Flash (zona segura).
void persist_if_dirty() {
    if (!g_persist_dirty) return;
    g_persist_dirty = false;
    flash::update_checksum(&g_persist);
    flash::save(g_persist);   // flash_safe_execute() bloquea al otro núcleo.
}

// Inserta `time_us` en el ranking (orden ascendente = mejor primero) si aplica.
void ranking_insert(uint32_t time_us, const char* name) {
    for (int i = 0; i < RANKING_SIZE; i++) {
        if (time_us < g_persist.ranking[i].time_us) {
            // Desplaza hacia abajo e inserta en la posición i.
            for (int j = RANKING_SIZE - 1; j > i; j--) {
                g_persist.ranking[j] = g_persist.ranking[j - 1];
            }
            g_persist.ranking[i].time_us = time_us;
            std::strncpy(g_persist.ranking[i].name, name, USER_NAME_MAX - 1u);
            g_persist.ranking[i].name[USER_NAME_MAX - 1u] = '\0';
            g_persist_dirty = true;
            break;
        }
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Transiciones de ronda
// -----------------------------------------------------------------------------
static void start_round(const GameConfig& cfg) {
    // Actualiza la semilla persistida (última usada) y (re)siembra el PRNG.
    g_persist.last_seed = cfg.seed;
    g_persist_dirty = true;
    g_rng.set_seed(cfg.seed != 0u ? cfg.seed : get_rand_32());

    g_round_active = true;
    g_round_start_us = time_us_32();
    audio::play_countdown();
}

static void end_round() {
    g_round_active = false;
    leds_off();
    audio::play_game_over();
    g_last_activity_us = time_us_32();
    persist_if_dirty();
}

// -----------------------------------------------------------------------------
// Bucle principal del juego
// -----------------------------------------------------------------------------
void run() {
    while (true) {
        // --- 1) Procesa eventos de control (enviados por la web, Core 1). ---
        ControlEvent ev;
        while (control_try_pop(&ev)) {
            switch (ev) {
                case ControlEvent::START_ROUND:
                    if (!g_round_active) start_round(config_get());
                    break;
                case ControlEvent::STOP_ROUND:
                    if (g_round_active) end_round();
                    break;
                case ControlEvent::RESET_STATS:
                    stats_reset();
                    break;
                default:
                    break; // CONFIG_CHANGED: la config se relee en cada iteración.
            }
        }

        GameConfig cfg = config_get();

        // --- 2) Estado IDLE: espera iniciar (pulsación o evento web). ---
        if (!g_round_active) {
            if (g_button_pending) {
                g_button_pending = false;
                g_last_activity_us = time_us_32();
                start_round(cfg);
            } else if ((time_us_32() - g_last_activity_us) > IDLE_SLEEP_AFTER_US) {
                // Tras 3 minutos sin actividad, entra en bajo consumo (WFI).
                // Se despierta con una pulsación (IRQ GPIO) o con el timer
                // periódico (IDLE_WAKE_PERIOD_MS) para revisar la cola web.
                leds_off();
                __wfi();
            } else {
                sleep_ms(5);       // Pequeña espera para no saturar el núcleo.
            }
            continue;
        }

        // --- 3) Estado RUNNING: presenta un estímulo. ---
        // Apaga todos los LEDs y espera un intervalo aleatorio entre estímulos.
        leds_off();
        uint32_t span = cfg.stimulus_max_ms - cfg.stimulus_min_ms + 1u;
        uint32_t delay_ms = cfg.stimulus_min_ms + g_rng.next_range(span);
        sleep_ms(delay_ms);

        // Descarta pulsaciones "tempranas" durante el intervalo.
        g_button_pending = false;

        // Enciende un LED aleatorio y registra el instante inicial.
        uint8_t target = (uint8_t)g_rng.next_range(LED_COUNT);
        led_on(target);
        uint32_t t0 = time_us_32();

        // Tolerancia en microsegundos. En FREE_PLAY no hay timeout (valor "infinito").
        uint32_t tolerance_us =
            (cfg.mode == GameMode::FREE_PLAY)
                ? UINT32_MAX
                : difficulty_tolerance_ms(cfg.difficulty) * 1000u;

        // --- 4) Espera la pulsación o el timeout. ---
        uint32_t reaction_us = 0;
        bool hit = false, miss = false, timeout = false;
        while (true) {
            if (g_button_pending) {
                g_button_pending = false;
                uint8_t btn = g_button_index;
                reaction_us = g_button_ts_us - t0;   // Resta segura ante wrap.

                if (btn == target && reaction_us <= tolerance_us) {
                    hit = true;          // Botón correcto y a tiempo.
                } else if (btn == target) {
                    timeout = true;      // Correcto pero demasiado lento.
                } else {
                    miss = true;         // Botón incorrecto.
                }
                break;
            }

            // Timeout por tolerancia (solo en modos con límite de tiempo).
            if ((time_us_32() - t0) > tolerance_us) {
                timeout = true;
                break;
            }

            // Fin de ronda por tiempo (modo TIMED).
            if (cfg.mode == GameMode::TIMED &&
                (time_us_32() - g_round_start_us) > cfg.round_seconds * 1000000u) {
                end_round();
                break;
            }

            // Detención solicitada desde la web.
            ControlEvent ev2;
            if (control_try_pop(&ev2)) {
                if (ev2 == ControlEvent::STOP_ROUND) { end_round(); break; }
                if (ev2 == ControlEvent::RESET_STATS) { stats_reset(); }
            }
        }

        // Si la ronda terminó durante la espera, vuelve al inicio del bucle.
        if (!g_round_active) {
            continue;
        }

        // --- 5) Evalúa el resultado y da feedback. ---
        stats_record(hit ? reaction_us : 0u, hit, timeout);

        if (hit) {
            audio::play_hit();
            // Nuevo récord personal / global -> actualiza ranking y persiste.
            if (reaction_us < g_persist.best_time_us) {
                g_persist.best_time_us = reaction_us;
                g_persist_dirty = true;
            }
            ranking_insert(reaction_us, cfg.user_name);
        } else if (miss) {
            audio::play_miss();
        } else {
            audio::play_timeout();
        }

        // En MUERTE SÚBITA un fallo (o timeout) termina la ronda.
        if (cfg.mode == GameMode::SUDDEN_DEATH && !hit) {
            end_round();
            continue;
        }

        // Guarda en Flash de forma esporádica (cuando haya cambios pendientes).
        persist_if_dirty();
    }
}

bool is_round_active() {
    return g_round_active;
}

} // namespace game
