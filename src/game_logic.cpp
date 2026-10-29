/**
 * @file   game_logic.cpp
 * @brief  Motor del juego de reacción (Core 0).
 *
 * Responsabilidades:
 *  - Configurar LEDs (salidas) y pulsadores (entradas con debounce por PIO).
 *  - Medir la reacción con resolución de microsegundos:
 *      * El timestamp lo captura la IRQ del PIO con time_us_32() (hardware_timer),
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
 *  - hardware_pio / pio_sm_init (debounce de pulsadores en PIO):
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pio
 *  - hardware_timer / time_us_32:
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_timer
 *  - hardware_dma / channel_config_set_ring / channel_config_set_dreq:
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_dma
 */

#include "game_logic.hpp"

#include <cstring>
#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "hardware/structs/sio.h"
#include "pico/time.h"
#include "pico/rand.h"
#include "pico/low_power.h"

#include "audio.hpp"
#include "flash_storage.hpp"
#include "pin_defs.hpp"
#include "led_driver.hpp"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"

#include "button_debounce.pio.h"

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

// Estado de Simón Dice (secuencia de memoria creciente).
#define SIMON_MAX_SEQ 64u            ///< Longitud máxima de la secuencia.
static uint8_t g_sequence[SIMON_MAX_SEQ];  ///< Secuencia de LEDs (índices 0..4).
static uint8_t g_seq_length = 1;     ///< Longitud actual de la secuencia.

// Datos persistentes (récords, nombre, semilla).
static flash::PersistentData g_persist;
static bool g_persist_dirty = false;   ///< true si hay que escribir a Flash.

// -----------------------------------------------------------------------------
// Modo de bajo consumo (DORMANT, < 2 mA)
// -----------------------------------------------------------------------------
// Tras idle_sleep_min minutos de inactividad (configurable desde /display y
// persistido en Flash), el Core 0 inicia el protocolo de dormancia: pide al
// Core 1 que apague el Wi-Fi (cyw43_arch_deinit) y, cuando responde, entra en
// reposo. Si sleep_enabled es false, nunca duerme.
#define IDLE_WAKE_PERIOD_MS   50u                    ///< Despertar periódico (fallback).
#define SLEEP_READY_TIMEOUT_MS 1000u                 ///< Timeout del handshake.

static uint32_t g_last_activity_us = 0;   ///< Última actividad (pulsación/ronda).
static struct repeating_timer g_wake_timer;

// Callback del temporizador de "despertar periódico" (no hace nada: solo
// interrumpe el WFI para que el bucle revise la cola de eventos).
static bool wake_timer_cb(struct repeating_timer*) {
    return true;   // true = seguir repitiendo.
}

// -----------------------------------------------------------------------------
// Entrada al modo de bajo consumo (degradado con FreeRTOS)
// -----------------------------------------------------------------------------
// Con FreeRTOS SMP NO se usa el DORMANT real (low_power_dormant_until_gpio_pin_state)
// porque exige ambos núcleos en WFE. En su lugar se hace un sleep ligero: se pide
// al Core 1 que detenga los servicios del AP y se cede el núcleo con vTaskDelay.
// Devuelve false si el Core 1 no respondió a tiempo.
static bool enter_dormant() {
    // 1) Pide al Core 1 que apague los servicios de red (DNS/DHCP del AP).
    g_sleep_ready = false;
    g_sleep_request = true;

    // 2) Espera la respuesta del Core 1 (con timeout).
    uint32_t start = time_us_32();
    while (!g_sleep_ready) {
        if ((time_us_32() - start) > SLEEP_READY_TIMEOUT_MS * 1000u) {
            g_sleep_request = false;   // Aborta: el Core 1 no respondió.
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // 3) Sleep ligero (sin DORMANT real): cede el núcleo hasta la próxima
    //    actividad. El despertar lo maneja la IRQ de botones o el timer.
    vTaskDelay(pdMS_TO_TICKS(IDLE_WAKE_PERIOD_MS));

    // 4) Libera al Core 1 para que re-inicialice la red si hacía falta.
    g_sleep_request = false;
    g_last_activity_us = time_us_32();
    return true;
}

// -----------------------------------------------------------------------------
// Debounce de pulsadores por PIO (ver button_debounce.pio)
// -----------------------------------------------------------------------------
// Cada pulsador tiene su propia state machine PIO que muestrea el pin y emite el
// flanco ya limpio. La CPU NO filtra: la IRQ solo captura time_us_32() y guarda
// índice/timestamp, tal como hacía la antigua IRQ GPIO, pero sin el chequeo de
// g_last_edge_us (ese rebote lo absorbe ahora el PIO).
//
// Mapeo PIO: botones 0..3 -> pio1 SM0..SM3; botón 4 -> pio0 SM1 (pio0 SM0 queda
// libre para el driver WS2812 cuando PICOREFLEX_WS2812 == 1).
static PIO  g_btn_pio[LED_COUNT];   ///< PIO de cada botón.
static uint g_btn_sm [LED_COUNT];   ///< State machine de cada botón.

// Handler común para las IRQ de los dos bloques PIO (se registra por separado en
// PIO0_IRQ_0 y PIO1_IRQ_0; distingue el bloque por el parámetro `pio`).
static void pio_button_irq(PIO pio) {
    // Captura el timestamp UNA vez, lo más cerca posible del flanco.
    uint32_t now = time_us_32();

    // Limpia el flag de IRQ 0 antes de drenar. No se pierde ningún dato: el índice
    // viaja por el RX FIFO de cada SM; un flanco nuevo durante el drenado vuelve a
    // subir el flag y dispara otra IRQ.
    pio_interrupt_clear(pio, 0);

    for (uint i = 0; i < LED_COUNT; i++) {
        if (g_btn_pio[i] != pio) continue;
        while (!pio_sm_is_rx_fifo_empty(pio, g_btn_sm[i])) {
            uint8_t idx = (uint8_t)pio_sm_get(pio, g_btn_sm[i]);
            g_button_index   = idx;
            g_button_ts_us   = now;
            g_button_pending = true;
        }
    }
}

static void pio0_button_irq(void) { pio_button_irq(pio0); }
static void pio1_button_irq(void) { pio_button_irq(pio1); }

// -----------------------------------------------------------------------------
// LEDs (delegado al driver común: GPIO o WS2812 según PICOREFLEX_WS2812)
// -----------------------------------------------------------------------------
static void leds_off() {
    led::off_all();
}

static void led_on(uint i) {
    led::set(i, true);
}

// -----------------------------------------------------------------------------
// Inicialización
// -----------------------------------------------------------------------------

// Aplica la configuración persistida (Flash) a la configuración en RAM.
static void apply_persist_to_config() {
    GameConfig cfg = config_get();
    std::strncpy(cfg.wifi_ssid, g_persist.wifi_ssid, sizeof(cfg.wifi_ssid) - 1u);
    cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1u] = '\0';
    std::strncpy(cfg.wifi_password, g_persist.wifi_password, sizeof(cfg.wifi_password) - 1u);
    cfg.wifi_password[sizeof(cfg.wifi_password) - 1u] = '\0';
    std::strncpy(cfg.ap_password, g_persist.ap_password, sizeof(cfg.ap_password) - 1u);
    cfg.ap_password[sizeof(cfg.ap_password) - 1u] = '\0';
    cfg.oled_enabled = (g_persist.oled_enabled != 0);
    cfg.display_controller = g_persist.display_controller;
    cfg.sleep_enabled = (g_persist.sleep_enabled != 0);
    cfg.idle_sleep_min = g_persist.idle_sleep_min;
    cfg.start_delay_s = g_persist.start_delay_s;
    cfg.mode = static_cast<GameMode>(g_persist.game_mode);
    cfg.difficulty = static_cast<Difficulty>(g_persist.game_difficulty);
    cfg.seed = g_persist.seed;
    cfg.round_seconds = g_persist.round_seconds;
    cfg.stimulus_min_ms = g_persist.stimulus_min_ms;
    cfg.stimulus_max_ms = g_persist.stimulus_max_ms;
    cfg.batt_calib = g_persist.batt_calib;
    std::strncpy(cfg.user_name, g_persist.user_name, sizeof(cfg.user_name) - 1u);
    cfg.user_name[sizeof(cfg.user_name) - 1u] = '\0';
    config_set(cfg);
}

// Copia los campos configurables a la estructura persistente (para guardar en Flash).
static void sync_config_to_persist() {
    GameConfig cfg = config_get();
    std::strncpy(g_persist.wifi_ssid, cfg.wifi_ssid, sizeof(g_persist.wifi_ssid) - 1u);
    g_persist.wifi_ssid[sizeof(g_persist.wifi_ssid) - 1u] = '\0';
    std::strncpy(g_persist.wifi_password, cfg.wifi_password, sizeof(g_persist.wifi_password) - 1u);
    g_persist.wifi_password[sizeof(g_persist.wifi_password) - 1u] = '\0';
    std::strncpy(g_persist.ap_password, cfg.ap_password, sizeof(g_persist.ap_password) - 1u);
    g_persist.ap_password[sizeof(g_persist.ap_password) - 1u] = '\0';
    g_persist.oled_enabled = cfg.oled_enabled ? 1 : 0;
    g_persist.display_controller = cfg.display_controller;
    g_persist.sleep_enabled = cfg.sleep_enabled ? 1 : 0;
    g_persist.idle_sleep_min = cfg.idle_sleep_min;
    g_persist.start_delay_s = cfg.start_delay_s;
    g_persist.game_mode = static_cast<uint8_t>(cfg.mode);
    g_persist.game_difficulty = static_cast<uint8_t>(cfg.difficulty);
    g_persist.seed = cfg.seed;
    g_persist.round_seconds = cfg.round_seconds;
    g_persist.stimulus_min_ms = cfg.stimulus_min_ms;
    g_persist.stimulus_max_ms = cfg.stimulus_max_ms;
    g_persist.batt_calib = cfg.batt_calib;
    std::strncpy(g_persist.user_name, cfg.user_name, sizeof(g_persist.user_name) - 1u);
    g_persist.user_name[sizeof(g_persist.user_name) - 1u] = '\0';
    g_persist_dirty = true;
}

// Forward declaration (definida más abajo).
static void start_round(const GameConfig& cfg);

void init() {
    // --- LEDs: salidas digitales, apagados al inicio. ---
    for (uint i = 0; i < LED_COUNT; i++) {
        gpio_init(g_led_pins[i]);
        gpio_set_dir(g_led_pins[i], GPIO_OUT);
        gpio_put(g_led_pins[i], false);
    }

    // --- Pulsadores: entradas con pull-up; debounce por PIO. ---
    // Los pines siguen siendo entradas GPIO normales (gpio_get sigue funcionando
    // para el disparador de dos botones y el trace DMA). La ruta de ENTRADA del PIO
    // lee el pad directamente sin tocar el mux GPIO, por lo que NO se llama
    // pio_gpio_init(): basta con gpio_init + pull-up + sm_config_set_in_pins().
    const float sm_div = (float)clock_get_hz(clk_sys) / (float)BUTTON_DEBOUNCE_SAMPLE_HZ;

    // El programa ocupa 26 de las 32 instrucciones del bloque: se carga UNA vez por
    // bloque PIO y todas sus máquinas comparten el mismo offset.
    const uint pio0_offset = pio_add_program(pio0, &button_debounce_program);
    const uint pio1_offset = pio_add_program(pio1, &button_debounce_program);

    for (uint i = 0; i < LED_COUNT; i++) {
        gpio_init(g_button_pins[i]);
        gpio_set_dir(g_button_pins[i], GPIO_IN);
        gpio_pull_up(g_button_pins[i]);

        PIO  pio;
        uint sm;
        uint offset;
        if (i < 4) { pio = pio1; sm = i; offset = pio1_offset; }
        else       { pio = pio0; sm = 1; offset = pio0_offset; }
        g_btn_pio[i] = pio;
        g_btn_sm[i]  = sm;

        pio_sm_claim(pio, sm);

        pio_sm_config c = button_debounce_program_get_default_config(offset);
        sm_config_set_in_pins(&c, g_button_pins[i]);  // "pin 0" del programa = este pin.
        sm_config_set_clkdiv(&c, sm_div);             // Reloj de muestreo ~1 MHz.
        pio_sm_init(pio, sm, offset, &c);

        pio_sm_put(pio, sm, i);              // Índice de botón -> TX FIFO (lo lee `pull`).
        pio_sm_set_enabled(pio, sm, true);   // Arranca la state machine.
    }

    // Interrupciones de los bloques PIO (IRQ flag 0, una por bloque).
    pio_set_irq0_source_enabled(pio0, pis_interrupt0, true);
    pio_set_irq0_source_enabled(pio1, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_button_irq);
    irq_set_exclusive_handler(PIO1_IRQ_0, pio1_button_irq);
    irq_set_enabled(PIO0_IRQ_0, true);
    irq_set_enabled(PIO1_IRQ_0, true);

    // --- "Forzar AP": si el botón 0 se mantiene presionado durante el arranque,
    // se salta STA y la Pico arranca directo en modo Access Point. ---
    if (gpio_get(g_button_pins[0]) == 0) {
        g_force_ap = true;
        printf("[game] Boton 0 presionado al arrancar -> forzando modo AP\n");
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
    apply_persist_to_config();

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

// Cuenta regresiva (start_delay_s segundos) y arranque de la ronda.
static void start_with_countdown(const GameConfig& cfg) {
    uint delay = cfg.start_delay_s;
    if (delay > 10u) delay = 10u;   // Límite de seguridad del conteo.
    for (uint i = 0; i < delay; i++) {
        watchdog_update();       // Cuenta regresiva (hasta ~10 s): refresca el watchdog.
        audio::play_tone(440u, 90u);   // Beep de conteo (1 por segundo).
        vTaskDelay(pdMS_TO_TICKS(910u));
    }
    watchdog_update();
    audio::play_tone(880u, 200u);      // "Ya".
    start_round(cfg);
}

// -----------------------------------------------------------------------------
// Transiciones de ronda
// -----------------------------------------------------------------------------
static void start_round(const GameConfig& cfg) {
    // Actualiza la semilla persistida (última usada) y (re)siembra el PRNG.
    g_persist.last_seed = cfg.seed;
    g_persist_dirty = true;
    g_rng.set_seed(cfg.seed != 0u ? cfg.seed : get_rand_32());

    // En Simón Dice, la secuencia arranca con un único elemento.
    if (cfg.mode == GameMode::SIMON_SAYS) {
        g_seq_length = 1;
        g_sequence[0] = (uint8_t)g_rng.next_range(LED_COUNT);
    }

    g_round_active = true;
    g_round_start_us = time_us_32();
}

static void end_round() {
    g_round_active = false;
    leds_off();
    audio::play_game_over();
    g_last_activity_us = time_us_32();
    persist_if_dirty();
}

// =============================================================================
// Simón Dice (secuencia de memoria creciente)
// =============================================================================
// Validación (igual que el juego clásico "Simon Says"):
//   - Correcto: si el jugador reproduce la secuencia entera, se agrega un elemento
//     aleatorio al final y se vuelve a mostrar la secuencia (un paso más larga).
//   - Incorrecto: botón equivocado (o timeout) -> Game Over + señal de error y la
//     secuencia se reinicia a longitud 1.
// La secuencia la genera el PRNG xorshift32 sembrado con la semilla, por lo que es
// DETERMINISTA: una misma semilla produce siempre la misma secuencia.

// Frecuencia (Hz) del tono de cada LED (notas de la escala, como Simón Dice).
static uint32_t simon_tone(uint i) {
    static const uint32_t tones[LED_COUNT] = {262u, 330u, 392u, 523u, 659u};
    return tones[i % LED_COUNT];
}

// Intervalo base de presentación de la secuencia según dificultad (ms).
static uint32_t simon_base_interval_ms(Difficulty d) {
    switch (d) {
        case Difficulty::EASY:   return 700u;
        case Difficulty::HARD:   return 300u;
        default:                 return 450u;   // NORMAL
    }
}

// Intervalo efectivo de presentación: acelera a medida que la secuencia crece
// ("el patrón se vuelve cada vez más rápido"), con un suelo del 40 % del base
// para que los LEDs sigan siendo distinguibles.
static uint32_t simon_show_interval_ms(Difficulty d) {
    uint32_t interval = simon_base_interval_ms(d);
    uint32_t floor_   = interval * 2u / 5u;
    for (uint32_t i = 1; i < g_seq_length && interval > floor_; i++) {
        interval -= interval / 25u;   // -4 % por elemento de secuencia.
    }
    return interval;
}

// Muestra la secuencia completa (enciende cada LED + su tono).
static void simon_show_sequence(const GameConfig& cfg) {
    uint32_t interval = simon_show_interval_ms(cfg.difficulty);
    for (uint i = 0; i < g_seq_length; i++) {
        watchdog_update();   // Presentación larga de la secuencia: refresca el watchdog.
        led::set(g_sequence[i], true);
        audio::play_tone(simon_tone(g_sequence[i]), interval * 2u / 3u);
        led::set(g_sequence[i], false);
        vTaskDelay(pdMS_TO_TICKS(interval / 3u));
    }
}

// Espera la pulsación de UN elemento y devuelve true si coincide con `expected`.
static bool simon_wait_input(const GameConfig& cfg, uint8_t expected) {
    // Timeout por elemento (generoso, basado en la dificultad).
    uint32_t timeout_ms = difficulty_tolerance_ms(cfg.difficulty) * 6u + 2000u;
    uint32_t t0 = time_us_32();
    while (g_round_active) {
        if (g_button_pending) {
            g_button_pending = false;
            uint8_t btn = g_button_index;
            // Feedback visual breve del botón presionado.
            led::set(btn, true);
            vTaskDelay(pdMS_TO_TICKS(90));
            led::set(btn, false);
            return (btn == expected);
        }
        if ((time_us_32() - t0) > timeout_ms * 1000u) {
            return false;   // Timeout.
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        watchdog_update();   // Espera de pulsación: refresca el watchdog.
    }
    return false;
}

// Ronda de Simón Dice: muestra y valida la secuencia en bucle hasta el fallo.
static void simon_says_round(const GameConfig& cfg) {
    while (g_round_active) {
        simon_show_sequence(cfg);

        // El jugador reproduce la secuencia.
        bool ok = true;
        for (uint i = 0; i < g_seq_length && ok; i++) {
            ok = simon_wait_input(cfg, g_sequence[i]);
        }

        if (!ok) {
            // Game Over: error + reinicio a longitud 1.
            audio::play_game_over();
            led::effect_lose();
            stats_record(0u, false, false, cfg.seed, cfg.difficulty);
            g_seq_length = 1;
            g_sequence[0] = (uint8_t)g_rng.next_range(LED_COUNT);
            end_round();
            return;
        }

        // Secuencia completa correcta: crece y repite.
        led::effect_win();
        stats_record(g_seq_length, true, false, cfg.seed, cfg.difficulty);
        if (g_seq_length < SIMON_MAX_SEQ) {
            g_sequence[g_seq_length] = (uint8_t)g_rng.next_range(LED_COUNT);
            g_seq_length++;
        }
        vTaskDelay(pdMS_TO_TICKS(600));   // Pausa entre secuencias.
    }
}

// -----------------------------------------------------------------------------
// Bucle principal del juego
// -----------------------------------------------------------------------------
void run() {
    while (true) {
        watchdog_update();   // Alimenta el watchdog del sistema desde el Core 0.

        // --- 1) Procesa eventos de control (enviados por la web, Core 1). ---
        ControlEvent ev;
        while (control_try_pop(&ev)) {
            switch (ev) {
                case ControlEvent::START_ROUND:
                    if (!g_round_active) start_with_countdown(config_get());
                    break;
                case ControlEvent::STOP_ROUND:
                    if (g_round_active) end_round();
                    break;
                case ControlEvent::RESET_STATS:
                    stats_reset();
                    break;
                case ControlEvent::CONFIG_CHANGED:
                    // La web cambió la configuración: persiste los campos
                    // configurables en Flash.
                    printf("[game] CONFIG_CHANGED: persistir\n");
                    sync_config_to_persist();
                    persist_if_dirty();
                    printf("[game] persistido\n");
                    g_persist_done = true;   // Avisa al Core 1 (web) que ya guardó.
                    break;
                default:
                    break;
            }
        }

        GameConfig cfg = config_get();

        // --- 2) Estado IDLE: espera el disparador de inicio. ---
        if (!g_round_active) {
            if (g_button_pending) {
                g_button_pending = false;
                // Disparador: cualquier botón inicia la ronda.
                g_last_activity_us = time_us_32();
                start_with_countdown(cfg);
            } else if (cfg.sleep_enabled &&
                       (uint64_t)(uint32_t)(time_us_32() - g_last_activity_us) >
                           (uint64_t)cfg.idle_sleep_min * 60u * 1000000ull) {
                // Tras idle_sleep_min minutos sin actividad, entra en reposo,
                // PERO solo si el Wi-Fi está apagado: con la web activa (STA o AP)
                // hay que mantener el servidor HTTP accesible. Si el handshake con
                // el Core 1 falla, cae en un WFI como respaldo.
                SystemStatus st = system_status_get();
                if (st.wifi_sta || st.wifi_ap) {
                    // Wi-Fi activo: no dormir; refresca el contador de inactividad
                    // para volver a evaluar pasados otros 3 minutos.
                    g_last_activity_us = time_us_32();
                    sleep_ms(5);
                } else {
                    leds_off();
                    if (!enter_dormant()) {
                        __wfi();   // Respaldo: sleep ligero si no se pudo dormir.
                    }
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(5));   // Pequeña espera para no saturar.
            }
            continue;
        }

        // --- 3) Modo SIMÓN DICE: secuencia de memoria creciente. ---
        if (cfg.mode == GameMode::SIMON_SAYS) {
            simon_says_round(cfg);
            continue;
        }

        // --- 4) Estado RUNNING (reacción): presenta un estímulo. ---
        // Apaga todos los LEDs y espera un intervalo aleatorio entre estímulos.
        leds_off();
        uint32_t span = cfg.stimulus_max_ms - cfg.stimulus_min_ms + 1u;
        uint32_t delay_ms = cfg.stimulus_min_ms + g_rng.next_range(span);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

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

            watchdog_update();   // Espera de pulsación (FREE_PLAY puede ser indefinida).
        }

        // Si la ronda terminó durante la espera, vuelve al inicio del bucle.
        if (!g_round_active) {
            continue;
        }

        // --- 5) Evalúa el resultado y da feedback. ---
        stats_record(hit ? reaction_us : 0u, hit, timeout, cfg.seed, cfg.difficulty);

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
