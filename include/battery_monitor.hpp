/**
 * @file   battery_monitor.hpp
 * @brief  Medición de tensión de batería (ADC) y estimación de porcentaje.
 *
 * La batería se mide con un divisor resistivo EXTERNO en GPIO26 (ADC0):
 *   R1 = 10 kΩ (batería -> pin ADC), R2 = 22 kΩ (pin ADC -> GND).
 *   Vbat = Vpin * (R1 + R2) / R2 = Vpin * 32 / 22.
 *
 * Se usa GPIO26 porque está libre en la Pico W; GPIO29 (ADC3) está ocupado por
 * el SPI del Wi-Fi y por el divisor interno VSYS/3.
 *
 * Referencias:
 *  - hardware_adc: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc
 */

#ifndef PICO_REFLEX_BATTERY_MONITOR_HPP
#define PICO_REFLEX_BATTERY_MONITOR_HPP

#include "pico/stdlib.h"

#include "config.hpp"   // BatteryCalibration (curva de descarga configurable).

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

// Aplica una calibración de curva (tabla por tramos + compensación de carga).
// Los valores se validan internamente para mantener un rango sano.
void set_calibration(const BatteryCalibration& calib);

// Devuelve la calibración activa.
BatteryCalibration calibration();

} // namespace battery

#endif // PICO_REFLEX_BATTERY_MONITOR_HPP
