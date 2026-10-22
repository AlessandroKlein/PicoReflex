/**
 * @file   display_driver.cpp
 * @brief  Driver del OLED SSD1306/SH1106 128x64 por I2C1 con framebuffer y texto 5x7.
 *
 * Protocolo I2C (compartido por SSD1306 y SH1106):
 *  - Toda transacción comienza con un "byte de control":
 *      * 0x00  ->  los bytes siguientes son COMANDOS.
 *      * 0x40  ->  los bytes siguientes son DATOS (GDDRAM / framebuffer).
 *
 * Organización de memoria (modo "horizontal addressing"):
 *  - 128 columnas x 64 filas = 8 "páginas" de 8 filas cada una.
 *  - Cada byte del framebuffer representa UNA columna vertical de 8 píxeles
 *    (bit 0 = fila superior de la página).
 *
 * Referencias:
 *  - i2c_write_blocking(): https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_i2c
 *  - Datasheet SSD1306 (Solomon Systech).
 */

#include "display_driver.hpp"

#include <cstdio>
#include <cstring>
#include "hardware/i2c.h"

#include "pin_defs.hpp"
#include "font5x7.hpp"

namespace display {

// -----------------------------------------------------------------------------
// Framebuffer y estado
// -----------------------------------------------------------------------------
static uint8_t g_fb[OLED_PAGES * OLED_WIDTH]; ///< Framebuffer (8 páginas x 128).
static bool    g_ready = false;               ///< true si el display inicializó.
static uint8_t g_oled_addr = OLED_ADDR;       ///< Dirección I2C detectada.
static Controller g_controller = Controller::SSD1306;  ///< Controlador del panel.

// -----------------------------------------------------------------------------
// Escáner I2C (auto-detección de la dirección del OLED)
// -----------------------------------------------------------------------------
// Prueba una escritura de 1 byte (comando 0x00) y comprueba si hay ACK.
// Referencia: i2c_write_blocking() devuelve PICO_ERROR_GENERIC en un NACK.
static bool i2c_probe(uint8_t addr) {
    uint8_t dummy = 0x00;
    int rc = i2c_write_blocking(I2C_DISPLAY, addr, &dummy, 1, false);
    return (rc == 1);
}

// Detecta la dirección del SSD1306: 0x3C (SA0 a GND) o 0x3D (SA0 a 3V3).
static void detect_address() {
    if (i2c_probe(OLED_ADDR))      { g_oled_addr = OLED_ADDR; return; }
    if (i2c_probe(OLED_ADDR_ALT))  { g_oled_addr = OLED_ADDR_ALT; return; }
    g_oled_addr = OLED_ADDR;       // Por defecto si no se detecta nada.
}

// -----------------------------------------------------------------------------
// Helpers I2C
// -----------------------------------------------------------------------------
// Envía comandos al SSD1306 (byte de control 0x00 + comandos).
static int send_commands(const uint8_t* cmds, size_t n) {
    // Envía el byte de control 0x00 SIN stop, luego los comandos CON stop.
    uint8_t ctrl = 0x00;
    int rc = i2c_write_blocking(I2C_DISPLAY, g_oled_addr, &ctrl, 1, true);
    if (rc == PICO_ERROR_GENERIC) return rc;   // NACK: no hay dispositivo.
    return i2c_write_blocking(I2C_DISPLAY, g_oled_addr, cmds, n, false);
}

// -----------------------------------------------------------------------------
// Inicialización
// -----------------------------------------------------------------------------
bool init(Controller controller) {
    g_controller = controller;

    // Configura I2C1 UNA sola vez. Al cambiar de controlador en runtime se vuelve
    // a llamar a init(), y no hay que re-configurar el periférico I2C (ni perder
    // su estado), así que se guarda con un flag estático.
    static bool i2c_configured = false;
    if (!i2c_configured) {
        i2c_init(I2C_DISPLAY, I2C_BAUDRATE);
        gpio_set_function(I2C_DISPLAY_SDA, GPIO_FUNC_I2C);
        gpio_set_function(I2C_DISPLAY_SCL, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_DISPLAY_SDA);
        gpio_pull_up(I2C_DISPLAY_SCL);
        i2c_configured = true;
    }

    // Detecta la dirección I2C del panel (0x3C o 0x3D).
    detect_address();

    // Secuencias de inicialización separadas por controlador (128x64).
    // Cada par (0xXX, 0xYY) es un comando con argumento.
    //   - SSD1306: byte-idéntica a la secuencia histórica (charge-pump 0x8D,0x14,
    //     addressing horizontal 0x20,0x00, COM 0xDA,0x12). CERO regresión.
    //   - SH1106 (módulos 1.3"): convertidor DC-DC interno (0xAD,0x8B) en lugar
    //     del charge-pump, addressing por PÁGINA (0x20,0x02), contraste máximo
    //     (0x81,0xFF) y COM secuencial (0xDA,0x02). El offset de columna +2 se
    //     aplica en show() (0x21,0x02,0x7F).
    static const uint8_t kInitSSD1306[] = {
        0xAE,        // 1. Display OFF.
        0xD5, 0x80,  // 2. Divisor de reloj / frecuencia del oscilador.
        0xA8, 0x3F,  // 3. Multiplex ratio = 63 (64 líneas).
        0xD3, 0x00,  // 4. Display offset = 0.
        0x40,        // 5. Línea de inicio = 0.
        0x8D, 0x14,  // 6. Charge pump ON (necesario en OLED de 3.3V).
        0x20, 0x00,  // 7. Modo de direccionamiento: horizontal.
        0xA1,        // 8. Segment remap (col 127 -> SEG0).
        0xC8,        // 9. Dirección de scan COM (remap).
        0xDA, 0x12,  // 10. Configuración de pines COM (alternativa, SSD1306).
        0x81, 0xCF,  // 11. Contraste (0xCF = alto).
        0xD9, 0xF1,  // 12. Periodo de pre-carga.
        0xDB, 0x40,  // 13. Nivel VCOMH deselect.
        0xA4,        // 14. Contenido desde GDDRAM (no "all on").
        0xA6,        // 15. Display normal (no invertido).
        0xAF,        // 16. Display ON.
    };
    static const uint8_t kInitSH1106[] = {
        0xAE,        // 1. Display OFF.
        0xD5, 0x80,  // 2. Divisor de reloj / frecuencia del oscilador.
        0xA8, 0x3F,  // 3. Multiplex ratio = 63 (64 líneas).
        0xD3, 0x00,  // 4. Display offset = 0.
        0x40,        // 5. Línea de inicio = 0.
        0xAD, 0x8B,  // 6. Convertidor DC-DC interno ON (SH1106, no charge-pump).
        0x20, 0x02,  // 7. Modo de direccionamiento: por PÁGINA (1.3" ignora el horizontal).
        0xA1,        // 8. Segment remap (col 127 -> SEG0).
        0xC8,        // 9. Dirección de scan COM (remap).
        0xDA, 0x02,  // 10. Configuración de pines COM (secuencial, SH1106).
        0x81, 0xFF,  // 11. Contraste máximo (SH1106 1.3").
        0xD9, 0xF1,  // 12. Periodo de pre-carga.
        0xDB, 0x40,  // 13. Nivel VCOMH deselect.
        0xA4,        // 14. Contenido desde GDDRAM (no "all on").
        0xA6,        // 15. Display normal (no invertido).
        0xAF,        // 16. Display ON.
    };

    const uint8_t* seq = (g_controller == Controller::SH1106) ? kInitSH1106 : kInitSSD1306;
    size_t         len = (g_controller == Controller::SH1106) ? sizeof(kInitSH1106) : sizeof(kInitSSD1306);

    int rc = send_commands(seq, len);
    g_ready = (rc != PICO_ERROR_GENERIC);

    if (g_ready) {
        clear();
        show();
    }
    return g_ready;
}

Controller controller() {
    return g_controller;
}

// -----------------------------------------------------------------------------
// Framebuffer
// -----------------------------------------------------------------------------
void set_power(bool on) {
    if (!g_ready) return;
    uint8_t cmd = on ? 0xAF : 0xAE;   // Display ON / OFF.
    send_commands(&cmd, 1);
}

void clear() {
    for (size_t i = 0; i < sizeof(g_fb); i++) {
        g_fb[i] = 0x00;
    }
}

void show() {
    if (!g_ready) return;

    // Establece la ventana de direccionamiento (columnas y páginas).
    if (g_controller == Controller::SH1106) {
        // SH1106 (132 columnas): el área visible de 128 px empieza en la
        // columna 2, por lo que se aplica un offset de columna +2.
        static const uint8_t window[] = {
            0x21, 0x02, OLED_WIDTH - 1u,   // Column address: 2..127 (offset +2).
            0x22, 0x00, OLED_PAGES - 1u,   // Page address range.
        };
        send_commands(window, sizeof(window));
    } else {
        // SSD1306 (128 columnas): columnas 0..127, páginas 0..7.
        static const uint8_t window[] = {
            0x21, 0x00, OLED_WIDTH - 1u,   // Column address: 0..127.
            0x22, 0x00, OLED_PAGES - 1u,   // Page address range.
        };
        send_commands(window, sizeof(window));
    }

    // Vuelca el framebuffer completo. Con direccionamiento horizontal, el
    // controlador auto-incrementa columna/página tras cada byte.
    uint8_t ctrl = 0x40;
    i2c_write_blocking(I2C_DISPLAY, g_oled_addr, &ctrl, 1, true);           // 0x40, sin stop.
    i2c_write_blocking(I2C_DISPLAY, g_oled_addr, g_fb, sizeof(g_fb), false); // datos, con stop.
}

// -----------------------------------------------------------------------------
// Texto 5x7
// -----------------------------------------------------------------------------
void draw_char(uint8_t col, uint8_t page, char c) {
    if (page >= OLED_PAGES) return;
    if (c < 0x20 || c > 0x7E) c = ' ';

    const uint8_t* glyph = FONT5X7[c - 0x20];
    uint8_t* fb = &g_fb[page * OLED_WIDTH];

    // Dibuja las 5 columnas del glifo. La 6ta columna queda en blanco (espaciado).
    for (int i = 0; i < 5; i++) {
        uint8_t x = col + i;
        if (x < OLED_WIDTH) {
            fb[x] = glyph[i];
        }
    }
}

void draw_string(uint8_t col, uint8_t page, const char* s) {
    while (*s) {
        draw_char(col, page, *s++);
        col += 6;   // 5 píxeles del glifo + 1 de espaciado.
        if (col >= OLED_WIDTH - 5) break;   // Evita desbordar el borde derecho.
    }
}

void draw_printf(uint8_t col, uint8_t page, const char* fmt, ...) {
    char buf[32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    draw_string(col, page, buf);
}

} // namespace display
