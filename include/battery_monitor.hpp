/**
 * @file   battery_monitor.hpp
 * @brief  Medición de tensión de batería (VSYS) y estimación de porcentaje.
 *
 * En la Pico W, el pin GPIO29 (ADC3) mide VSYS a través de un divisor resistivo
 * interno de 3:1 (VSYS/3), y ADEMÁS es el reloj SPI (WL_CLK) del chip CYW43439.
 * Por eso toda lectura del ADC debe hacerse mientras el CYW43 NO esté usando el
 * bus SPI (ver board header boards/pico_w.h: CYW43_USES_VSYS_PIN).
 *
 * Referencias:
 *  - hardware_adc: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc
 *  - pico_cyw43_arch (cyw43_arch_lwip_begin/end):
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#pico_cyw43_arch
 */

#ifndef PICO_REFLEX_BATTERY_MONITOR_HPP
#define PICO_REFLEX_BATTERY_MONITOR_HPP

#include "pico/stdlib.h"

namespace battery {

// Inicializa el ADC. Debe llamarse UNA vez (idealmente en el Core 0).
void init();

// Lee la tensión de la batería en milivoltios (mV), promediando varias muestras.
// Esta función adquiere el lock del CYW43 para no chocar con el SPI del Wi-Fi.
uint32_t read_voltage_mv();

// Convierte la tensión (mV) en porcentaje de carga (0..100) usando una curva
// de descarga LiPo/Li-Ion de 1 celda (4.2V = 100%, 3.7V = 50%, 3.0V = 0%).
uint8_t voltage_to_percent(uint32_t voltage_mv);

// Devuelve true si la batería está baja (< 15 %) y debe mostrarse una alerta.
bool is_low_battery(uint8_t percent);

} // namespace battery

#endif // PICO_REFLEX_BATTERY_MONITOR_HPP
