/**
 * @file   led_driver.cpp
 * @brief  Implementación de los LEDs (GPIO o WS2812 según PICOREFLEX_WS2812).
 *
 * Ver include/led_driver.hpp para el detalle de los dos modos.
 * WS2812 usa PIO (programa ws2812.pio) con salida de 800 kHz en GPIO28.
 */
#include "led_driver.hpp"

#include <cstring>
#include "pico/time.h"
#include "pin_defs.hpp"

#if PICOREFLEX_WS2812

// =============================================================================
// Implementación WS2812 (NeoPixel) — PIO
// =============================================================================
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

#define WS2812_PIN 28u   ///< Pin de datos de la tira WS2812 (GPIO28, libre).

static PIO g_pio = nullptr;
static uint g_sm  = 0;   ///< State machine del PIO.

namespace led {

void init() {
    g_pio = pio0;
    g_sm  = 0;
    uint offset = pio_add_program(g_pio, &ws2812_program);
    ws2812_program_init(g_pio, g_sm, offset, WS2812_PIN, 800000.f, false);
    off_all();
}

// Envía un píxel en formato GRB (el WS2812 espera Green-Red-Blue).
static void put_pixel_grb(uint32_t grb) {
    pio_sm_put_blocking(g_pio, g_sm, grb << 8u);
}

// Convierte (r,g,b) a GRB de 24 bits y lo envía.
void set_color(uint i, uint8_t r, uint8_t g, uint8_t b) {
    (void)i;   // Los 5 "LEDs lógicos" mapean a los primeros 5 píxeles de la tira.
    uint32_t grb = ((uint32_t)g << 16u) | ((uint32_t)r << 8u) | (uint32_t)b;
    put_pixel_grb(grb);
}

void set(uint i, bool on) {
    // "Encendido" = blanco; "apagado" = negro.
    set_color(i, on ? 255 : 0, on ? 255 : 0, on ? 255 : 0);
}

void off_all() {
    for (uint i = 0; i < LED_COUNT; i++) set_color(i, 0, 0, 0);
}

void effect_win() {
    // Barrido verde de izquierda a derecha, 3 pasadas.
    for (uint pass = 0; pass < 3; pass++) {
        for (uint i = 0; i < LED_COUNT; i++) {
            set_color(i, 0, 255, 0);
            sleep_ms(60);
            set_color(i, 0, 0, 0);
        }
    }
    off_all();
}

void effect_lose() {
    // Parpadeo rojo, 3 veces.
    for (uint pass = 0; pass < 3; pass++) {
        for (uint i = 0; i < LED_COUNT; i++) set_color(i, 255, 0, 0);
        sleep_ms(200);
        off_all();
        sleep_ms(200);
    }
}

} // namespace led

#else // !PICOREFLEX_WS2812

// =============================================================================
// Implementación GPIO (5 LEDs con resistencia en GPIO2..6)
// =============================================================================
#include "hardware/gpio.h"

namespace led {

static const uint k_led_pins[LED_COUNT] = LED_PINS;

void init() {
    for (uint i = 0; i < LED_COUNT; i++) {
        gpio_init(k_led_pins[i]);
        gpio_set_dir(k_led_pins[i], GPIO_OUT);
        gpio_put(k_led_pins[i], false);
    }
}

void set(uint i, bool on) {
    gpio_put(k_led_pins[i], on);
}

void set_color(uint i, uint8_t r, uint8_t g, uint8_t b) {
    // Sin color real en GPIO: se mapea a encendido si hay algún canal distinto de 0.
    set(i, (r | g | b) != 0);
}

void off_all() {
    for (uint i = 0; i < LED_COUNT; i++) set(i, false);
}

void effect_win() {
    // Parpadeo rápido de todos los LEDs (señal de acierto).
    for (uint pass = 0; pass < 3; pass++) {
        for (uint i = 0; i < LED_COUNT; i++) set(i, true);
        sleep_ms(120);
        off_all();
        sleep_ms(120);
    }
}

void effect_lose() {
    // Parpadeo lento (señal de error).
    for (uint pass = 0; pass < 3; pass++) {
        for (uint i = 0; i < LED_COUNT; i++) set(i, true);
        sleep_ms(300);
        off_all();
        sleep_ms(300);
    }
}

} // namespace led

#endif // PICOREFLEX_WS2812
