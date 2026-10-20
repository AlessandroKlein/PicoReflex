/**
 * @file   pin_defs.hpp
 * @brief  Mapa de pines del proyecto PicoReflex (Raspberry Pi Pico W).
 *
 * Concentra TODA la asignación de pines en un único lugar para que sea fácil de
 * verificar contra el esquema de hardware (Idea.md, sección 4) y contra el
 * datasheet de la Raspberry Pi Pico W.
 *
 * Decisión de diseño (ver docs/DECISIONES.md):
 *  - Se usa la asignación propuesta en Idea.md, que no entra en conflicto entre sí.
 *  - El display usa I2C1 (GPIO14/GPIO15); el buzzer usa GPIO16 en PWM.
 *
 * Referencias de documentación oficial:
 *  - Asignación de funciones de pines (GPIO function select):
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio
 *  - ADC (GPIO26..29 -> ADC0..ADC3):
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc
 *  - PWM (cada GPIO mapea a un slice/canal):
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pwm
 *  - I2C:
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_i2c
 */

#ifndef PICO_REFLEX_PIN_DEFS_HPP
#define PICO_REFLEX_PIN_DEFS_HPP

#include "pico/stdlib.h"

// -----------------------------------------------------------------------------
// LEDs indicadores de reacción (salidas digitales)
// -----------------------------------------------------------------------------
// 5 LEDs conectados a GPIO 2..6. Cada LED corresponde al botón del mismo índice.
// Se asume lógica "LED encendido = GPIO en nivel ALTO" (cátodo a masa vía
// resistencia limitadora). Ver Idea.md sección 4.
#define LED_COUNT 5u                 ///< Cantidad de LEDs / botones del juego.
#define LED_PINS {2u, 3u, 4u, 5u, 6u} ///< Pines de los 5 LEDs (índice 0..4).

// -----------------------------------------------------------------------------
// Pulsadores (entradas digitales con pull-up interno)
// -----------------------------------------------------------------------------
// 5 pulsadores a GPIO 7..11. "Presionado" = nivel BAJO (pull-up activo).
// La interrupción de reacción se configura por FLANCO DE BAJADA.
#define BUTTON_PINS {7u, 8u, 9u, 10u, 11u} ///< Pines de los 5 pulsadores.

// Debounce software: tiempo mínimo entre dos flancos válidos de un pulsador.
// Documentación: gpio_set_irq_enabled_with_callback()
// https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio
#define BUTTON_DEBOUNCE_US 5000u   ///< Debounce de 5 ms en microsegundos.

// -----------------------------------------------------------------------------
// Display I2C (OLED SSD1306 de 128x64 píxeles)
// -----------------------------------------------------------------------------
// I2C1 en GPIO14 (SDA) y GPIO15 (SCL). En RP2040, I2C1 admite estos pines.
#define I2C_DISPLAY      i2c1        ///< Instancia de I2C usada por el display.
#define I2C_DISPLAY_SDA  14u         ///< GPIO SDA del display.
#define I2C_DISPLAY_SCL  15u         ///< GPIO SCL del display.
#define I2C_BAUDRATE     400u * 1000u ///< 400 kHz (fast mode).

#define OLED_ADDR        0x3Cu       ///< Dirección I2C por defecto del SSD1306.
#define OLED_ADDR_ALT    0x3Du       ///< Dirección alternativa (común en clones).
#define OLED_WIDTH       128u        ///< Ancho en píxeles.
#define OLED_HEIGHT      64u         ///< Alto en píxeles.
#define OLED_PAGES       (OLED_HEIGHT / 8u) ///< 8 páginas de 8 filas cada una.

// -----------------------------------------------------------------------------
// Buzzer pasivo (salida PWM)
// -----------------------------------------------------------------------------
// Buzzer pasivo en GPIO16. Se controla con un slice PWM para generar tonos.
// Ver: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pwm
#define BUZZER_PIN       16u         ///< GPIO del buzzer pasivo.

// -----------------------------------------------------------------------------
// Medición de batería (ADC)
// -----------------------------------------------------------------------------
// GPIO29 = ADC3. En la Pico W mide VSYS/3 mediante un divisor resistivo interno
// 3:1, y ADEMÁS es el reloj SPI (WL_CLK) del chip CYW43439. Por eso la lectura
// debe envolverse entre cyw43_thread_enter()/exit() (ver battery_monitor.cpp y
// docs/DUDAS.md punto 1).
#define VSYS_PIN         29u         ///< GPIO de medición de VSYS (ADC3).
#define VSYS_ADC_CHANNEL 3u          ///< Canal ADC correspondiente a GPIO29.

// Divisor resistivo interno de la Pico W: la tensión en el pin es VSYS/3.
#define VSYS_DIVIDER     3.0f

// -----------------------------------------------------------------------------
// LED onboard de la Pico W (chip CYW43)
// -----------------------------------------------------------------------------
// En la Pico W no hay LED en GPIO25: el LED onboard lo controla el chip inalámbrico.
// Se usa para indicar el estado de la conexión Wi-Fi.
// Ver: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#pico_cyw43_arch
#ifndef CYW43_WL_GPIO_LED_PIN
#define CYW43_WL_GPIO_LED_PIN 0u
#endif

#endif // PICO_REFLEX_PIN_DEFS_HPP
