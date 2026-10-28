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

// Detecta la dirección del OLED: 0x3C (SA0 a GND) o 0x3D (SA0 a 3V3).
static void detect_address() {
    if (i2c_probe(OLED_ADDR))      { g_oled_addr = OLED_ADDR;     printf("[display] OLED detectado en 0x3C\n"); return; }
    if (i2c_probe(OLED_ADDR_ALT))  { g_oled_addr = OLED_ADDR_ALT; printf("[display] OLED detectado en 0x3D\n"); return; }
    g_oled_addr = OLED_ADDR;       // Por defecto si no se detecta nada.
    printf("[display] OLED NO detectado (0x3C/0x3D): revisar SDA/SCL y alimentacion\n");
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
    //     (0x81,0xFF) y COM alternativa (0xDA,0x12). El offset de columna +2 se
    //     aplica en show() página por página (0xB0+page, col 0x02/0x10).
    static const uint8_t kInitSSD1306[] = {
        0xAE,        // 1. Display OFF.
        0xD5, 0x80,  // 2. Divisor de reloj / frecuencia del oscilador.
        0xA8, 0x3F,  // 3. Multiplex ratio = 63 (64 líneas).
        0xD3, 0x00,  // 4. Display offset = 0.
        0x40,        // 5. Línea de inicio = 0.
        0x8D, 0x14,  // 6. Charge pump ON (necesario en OLED de 3.3V).
        0xAD, 0x8B,  // 6b. DC-DC ON (SH1106; ignorado por SSD1306). Compat.
        0x20, 0x00,  // 7. Modo de direccionamiento: horizontal.
        0xA1,        // 8. Segment remap (col 127 -> SEG0).
        0xC8,        // 9. Dirección de scan COM (remap).
        0xDA, 0x12,  // 10. Configuración de pines COM (alternativa, SSD1306).
        0x81, 0xCF,  // 11. Contraste (0xCF = alto).
        0xD9, 0x22,  // 12. Periodo de pre-carga (valor probado en refs).
        0xDB, 0x35,  // 13. Nivel VCOMH deselect.
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
        0x31,        // 6c. Set pump voltage = 7.4V (evita VPP bajo).
        0x8D, 0x14,  // 6b. Charge pump ON (SSD1306; ignorado por SH1106). Compat.
        0x20, 0x02,  // 7. Modo de direccionamiento: por PÁGINA (1.3" ignora el horizontal).
        0xA1,        // 8. Segment remap (col 127 -> SEG0).
        0xC8,        // 9. Dirección de scan COM (remap).
        0xDA, 0x12,  // 10. Configuración de pines COM (alternativa, estándar 128x64).
        0x81, 0xFF,  // 11. Contraste máximo (SH1106 1.3").
        0xD9, 0x22,  // 12. Periodo de pre-carga (valor probado en refs).
        0xDB, 0x35,  // 13. Nivel VCOMH deselect.
        0xA4,        // 14. Contenido desde GDDRAM (no "all on").
        0xA6,        // 15. Display normal (no invertido).
        0xAF,        // 16. Display ON.
    };

    const uint8_t* seq = (g_controller == Controller::SH1106) ? kInitSH1106 : kInitSSD1306;
    size_t         len = (g_controller == Controller::SH1106) ? sizeof(kInitSH1106) : sizeof(kInitSSD1306);

    int rc = send_commands(seq, len);
    g_ready = (rc != PICO_ERROR_GENERIC);

    // Diagnóstico por serial: qué controlador se está inicializando y si la
    // secuencia de comandos se envió sin NACK. "Detectado en 0x3C" solo prueba
    // que la dirección I2C responde (SSD1306 y SH1106 responden igual en 0x3C);
    // esto confirma cuál de los dos se está usando.
    printf("[display] controlador=%s, init=%s\n",
           (g_controller == Controller::SH1106) ? "SH1106" : "SSD1306",
           g_ready ? "OK" : "FALLO");

    if (g_ready) {
        clear();
        show();
        self_test();   // Destello blanco de arranque (diagnóstico del panel).
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

    if (g_controller == Controller::SH1106) {
        // SH1106 (132 columnas): NO soporta las órdenes de rango del SSD1306
        // (0x21/0x22). Usa el modo PÁGINA: por cada página se fija la página
        // (0xB0+page) y la columna baja/alta con offset +2, y se vuelcan los
        // 128 bytes de esa página. El +2 deja fuera las 2 columnas fantasma.
        for (uint8_t page = 0; page < OLED_PAGES; page++) {
            const uint8_t cmds[] = {
                (uint8_t)(0xB0 | page),  // Page start address.
                0x02,                    // Low column  = 2 (offset +2).
                0x10,                    // High column = 0.
            };
            send_commands(cmds, sizeof(cmds));

            uint8_t ctrl = 0x40;
            i2c_write_blocking(I2C_DISPLAY, g_oled_addr, &ctrl, 1, true);   // 0x40, sin stop.
            i2c_write_blocking(I2C_DISPLAY, g_oled_addr,
                               &g_fb[page * OLED_WIDTH], OLED_WIDTH, false); // datos, con stop.
        }
        return;
    }

    // SSD1306 (128 columnas): direccionamiento horizontal (0x21/0x22) y un solo volcado.
    static const uint8_t window[] = {
        0x21, 0x00, OLED_WIDTH - 1u,   // Column address: 0..127.
        0x22, 0x00, OLED_PAGES - 1u,   // Page address range.
    };
    send_commands(window, sizeof(window));

    uint8_t ctrl = 0x40;
    i2c_write_blocking(I2C_DISPLAY, g_oled_addr, &ctrl, 1, true);           // 0x40, sin stop.
    i2c_write_blocking(I2C_DISPLAY, g_oled_addr, g_fb, sizeof(g_fb), false); // datos, con stop.
}

void self_test() {
    if (!g_ready) return;
    // "Display All ON" (0xA5) enciende TODOS los píxeles sin leer la RAM, así que
    // prueba de forma independiente el panel + charge-pump + alimentación. Si no
    // se ve un destello blanco de ~1 s, el problema es de hardware, no de RAM.
    uint8_t all_on = 0xA5;
    uint8_t resume = 0xA4;

    // Diagnóstico por serial: confirma que (1) este build se ejecuta realmente y
    // (2) si el propio comando 0xA5 fue aceptado (ACK) por el controlador. Con
    // ACK + sin destello, el fallo queda acotado al charge-pump / panel (hardware).
    printf("[display] self-test: enviando 0xA5 (all-on)...\n");
    int rc = send_commands(&all_on, 1);
    printf("[display] self-test: all-on rc=%d (%s)\n",
           rc, (rc == PICO_ERROR_GENERIC) ? "NACK" : "ACK");
    sleep_ms(1000);
    send_commands(&resume, 1);
    printf("[display] self-test: fin (0xA4)\n");
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
