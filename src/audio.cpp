/**
 * @file   audio.cpp
 * @brief  Implementación del feedback sonoro por PWM (buzzer pasivo).
 *
 * Generación de tono:
 *  - El reloj del sistema (clk_sys, 125 MHz por defecto en la RP2040) alimenta
 *    el slice PWM. Para obtener una frecuencia `f`, se configura el divisor de
 *    reloj (clkdiv) y el contador de "wrap" de modo que:
 *          f = clk_sys / (clkdiv * (wrap + 1))
 *  - El nivel del canal se fija a `wrap / 2` para un duty del 50 %.
 *
 * Usamos un divisor fijo (AUDIO_CLKDIV = 100) y derivamos `wrap` de la frecuencia
 * pedida. Esto mantiene el código simple y cubre el rango audible (aprox.
 * 20 Hz .. 8 kHz) sin desbordar el registro de wrap de 16 bits.
 *
 * Referencias:
 *  - pwm_get_default_config() / pwm_config_set_clkdiv() / pwm_config_set_wrap():
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pwm
 *  - clock_get_hz(): https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_clocks
 */

#include "audio.hpp"

#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/time.h"

#include "pin_defs.hpp"

namespace audio {

// -----------------------------------------------------------------------------
// Constantes de PWM
// -----------------------------------------------------------------------------
// Divisor de reloj del PWM. Con clk_sys = 125 MHz -> base = 1.25 MHz.
// Para f = 440 Hz  -> wrap = 1.25e6 / 440 - 1 ≈ 2840 (cabe en 16 bits).
// Para f = 20  Hz  -> wrap ≈ 62499 (cabe en 16 bits).
#define AUDIO_CLKDIV  100.0f

// -----------------------------------------------------------------------------
// Variables internas
// -----------------------------------------------------------------------------
static uint     g_slice = 0;   ///< Slice PWM asignado al buzzer.
static uint     g_chan  = 0;   ///< Canal PWM del buzzer dentro del slice.
static uint32_t g_base_hz = 0; ///< Frecuencia base = clk_sys / clkdiv.

// -----------------------------------------------------------------------------
// Inicialización
// -----------------------------------------------------------------------------
void init() {
    // Asigna la función PWM al pin del buzzer (GPIO16).
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);

    // Obtiene el slice/canal que corresponden a ese GPIO.
    g_slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    g_chan  = pwm_gpio_to_channel(BUZZER_PIN);

    // Configura el divisor de reloj del slice.
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, AUDIO_CLKDIV);

    // Inicializa el slice (sin arrancar todavía: el wrap se ajusta en play_tone).
    pwm_init(g_slice, &cfg, false);

    // Frecuencia base efectiva tras el divisor (clk_sys / clkdiv).
    g_base_hz = (uint32_t)(clock_get_hz(clk_sys) / AUDIO_CLKDIV);

    // Silencio inicial: canal a 0 y slice habilitado.
    pwm_set_chan_level(g_slice, g_chan, 0);
    pwm_set_enabled(g_slice, true);
}

// -----------------------------------------------------------------------------
// Reproducción de un tono
// -----------------------------------------------------------------------------
void play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0 || duration_ms == 0) {
        return;
    }

    // Evita wrap = 0 o desbordamiento: acota la frecuencia al rango soportado.
    if (freq_hz < 20u)   freq_hz = 20u;
    if (freq_hz > 8000u) freq_hz = 8000u;

    // wrap = base / f - 1. El "-1" viene de que el contador cuenta 0..wrap.
    uint32_t wrap = (g_base_hz / freq_hz) - 1u;
    pwm_set_wrap(g_slice, (uint16_t)wrap);

    // Duty del 50 %.
    pwm_set_chan_level(g_slice, g_chan, (uint16_t)(wrap / 2u));

    // Mantiene el tono el tiempo pedido.
    sleep_ms(duration_ms);

    // Apaga el canal (silencio) sin deshabilitar el slice.
    pwm_set_chan_level(g_slice, g_chan, 0);
}

// -----------------------------------------------------------------------------
// Efectos predefinidos
// -----------------------------------------------------------------------------
void play_hit() {
    play_tone(880u, 60u);   // La5
    play_tone(1175u, 80u);  // Re6 (ascendente = "correcto")
}

void play_miss() {
    play_tone(220u, 200u);  // La3 grave y sostenido = "incorrecto"
}

void play_timeout() {
    play_tone(440u, 70u);
    play_tone(440u, 70u);
}

void play_countdown() {
    play_tone(660u, 120u);  // Mi5
    play_tone(660u, 120u);
    play_tone(880u, 200u);  // La5 = "ya"
}

void play_game_over() {
    // Melodía descendente: Do5 - Sol4 - Mi4 - Do4.
    const uint32_t notes[] = {523u, 392u, 330u, 262u};
    for (uint32_t n : notes) {
        play_tone(n, 150u);
    }
}

} // namespace audio
