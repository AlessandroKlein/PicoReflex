/**
 * @file   led_driver.hpp
 * @brief  Abstracción de los LEDs del juego (GPIO o WS2812).
 *
 * El proyecto admite DOS tipos de LEDs, seleccionados en tiempo de compilación
 * con la macro `PICOREFLEX_WS2812` (ver config.hpp):
 *
 *  - `PICOREFLEX_WS2812 == 0` (por defecto): 5 LEDs GPIO con resistencia en
 *    GPIO2..6. `set(i, on)` enciende/apaga; `set_color` se mapea a encendido
 *    (color distinto de negro).
 *  - `PICOREFLEX_WS2812 == 1`: tira/diodos WS2812 (NeoPixel) en GPIO28 por PIO.
 *    Permite color por LED y efectos de luces al ganar/perder.
 *
 * La interfaz es común, así que el motor del juego no cambia según el tipo de LED.
 */
#ifndef PICO_REFLEX_LED_DRIVER_HPP
#define PICO_REFLEX_LED_DRIVER_HPP

#include <cstdint>
#include "pico/stdlib.h"

namespace led {

// Inicializa el hardware de LEDs (GPIO o PIO+WS2812). Llamar UNA vez en init().
void init();

// Enciende/apaga el LED `i` (0..4). En WS2812, "encender" = blanco.
void set(uint i, bool on);

// Fija el color RGB (0..255) del LED `i`. En modo GPIO, se mapea a encendido
// si (r|g|b) != 0, y a apagado en caso contrario.
void set_color(uint i, uint8_t r, uint8_t g, uint8_t b);

// Apaga todos los LEDs.
void off_all();

// Efecto de luz al GANAR (verde) y al PERDER (rojo). Bloquean ~1 s.
void effect_win();
void effect_lose();

} // namespace led

#endif // PICO_REFLEX_LED_DRIVER_HPP
