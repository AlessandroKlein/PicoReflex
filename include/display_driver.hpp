/**
 * @file   display_driver.hpp
 * @brief  Driver del display OLED SSD1306/SH1106 (128x64) por I2C1.
 *
 * El SSD1306 y el SH1106 son controladores OLED monocromo. Se comunican por I2C
 * con un protocolo sencillo:
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

#include "config.hpp"   // PICOREFLEX_DISPLAY_CONTROLLER (controlador por defecto)

namespace display {

// Controlador de display soportado.
enum class Controller : uint8_t {
    SSD1306 = 0,  ///< SSD1306 128x64 (sin offset de columna).
    SH1106  = 1,  ///< SH1106 132x64 (offset de columna +2).
};

// Inicializa I2C1 y el controlador OLED. Debe llamarse una vez.
// `controller` selecciona el chip: SSD1306 (por defecto) o SH1106. Para SH1106
// se aplica el offset de columna +2 (0x21, 0x02, 0x7F) y la config COM 0x02.
bool init(Controller controller =
    (PICOREFLEX_DISPLAY_CONTROLLER != 0) ? Controller::SH1106 : Controller::SSD1306);

// Enciende (true) o apaga (false) el display (comando SSD1306 0xAF / 0xAE).
void set_power(bool on);

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
