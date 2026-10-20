/**
 * @file   display_driver.hpp
 * @brief  Driver del display OLED SSD1306 (128x64) por I2C1.
 *
 * El SSD1306 es un controlador OLED monocromo. Se comunica por I2C con un
 * protocolo sencillo:
 *  - Byte de control 0x00 seguido de bytes de COMANDO.
 *  - Byte de control 0x40 seguido de bytes de DATOS (framebuffer).
 *
 * El framebuffer (1024 bytes) se mantiene en RAM y se vuelca completo al display
 * con show(), lo que simplifica el dibujado de texto.
 *
 * Referencias:
 *  - I2C (hardware_i2c): https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_i2c
 *  - Datasheet SSD1306 (Solomon Systech) para la secuencia de inicialización.
 */

#ifndef PICO_REFLEX_DISPLAY_DRIVER_HPP
#define PICO_REFLEX_DISPLAY_DRIVER_HPP

#include "pico/stdlib.h"
#include <stdarg.h>

namespace display {

// Inicializa I2C1 y el controlador SSD1306. Debe llamarse una vez.
bool init();

// Limpia el framebuffer (no lo envía al display).
void clear();

// Envía el framebuffer completo al display.
void show();

// Dibuja un carácter 5x7 en la posición (col, page). `page` es 0..7.
void draw_char(uint8_t col, uint8_t page, char c);

// Dibuja una cadena en (col, page). No hay salto de línea automático.
void draw_string(uint8_t col, uint8_t page, const char* s);

// Dibuja una cadena formateada (printf-style) en (col, page).
void draw_printf(uint8_t col, uint8_t page, const char* fmt, ...);

} // namespace display

#endif // PICO_REFLEX_DISPLAY_DRIVER_HPP
