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
 * Diferencias de inicialización entre controladores:
 *  - SSD1306: charge-pump interno (0x8D,0x14), addressing horizontal (0x20,0x00)
 *    y configuración COM 0x12.
 *  - SH1106 (módulos 1.3" 132x64): convertidor DC-DC interno (0xAD,0x8B),
 *    addressing por PÁGINA (0x20,0x02), contraste máximo (0x81,0xFF) y
 *    configuración COM 0x02. Además aplica un offset de columna +2 en show().
 *
 * La selección del controlador puede cambiar en tiempo de ejecución: init() puede
 * llamarse de nuevo con otro `Controller` sin re-configurar I2C1 (solo se
 * reenvía la secuencia de inicialización correspondiente).
 *
 * Referencias:
 *  - I2C (hardware_i2c): https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_i2c
 *  - Datasheet SSD1306 (Solomon Systech).
 *  - Datasheet SH1106 (Sinowealth).
 */

#ifndef PICO_REFLEX_DISPLAY_DRIVER_HPP
#define PICO_REFLEX_DISPLAY_DRIVER_HPP

#include "pico/stdlib.h"
#include <stdarg.h>

#include "config.hpp"   // PICOREFLEX_DISPLAY_CONTROLLER (controlador por defecto)

namespace display {

// Controlador de display soportado.
enum class Controller : uint8_t {
    SSD1306 = 0,  ///< SSD1306 128x64 (charge-pump interno, sin offset de columna).
    SH1106  = 1,  ///< SH1106 132x64 (DC-DC interno, offset de columna +2).
};

// Inicializa I2C1 y el controlador OLED. Puede llamarse de nuevo para re-inicializar
// con otro controlador (al cambiar de chip en runtime): la configuración de I2C1
// solo se aplica la primera vez, para no duplicar el estado del periférico.
// `controller` selecciona el chip: SSD1306 (charge-pump) o SH1106 (DC-DC interno,
// addressing por página y offset de columna +2).
bool init(Controller controller =
    (PICOREFLEX_DISPLAY_CONTROLLER != 0) ? Controller::SH1106 : Controller::SSD1306);

// Devuelve el controlador actualmente activo (SSD1306 o SH1106).
Controller controller();

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
