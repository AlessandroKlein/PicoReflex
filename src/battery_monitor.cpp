/**
 * @file   battery_monitor.cpp
 * @brief  Implementación de la medición de batería por ADC (divisor externo).
 *
 * Detalles técnicos:
 *  - ADC de la RP2040: 12 bits (0..4095), tensión de referencia = 3.3 V.
 *  - Divisor resistivo EXTERNO: R1 = 10 kΩ (batería -> pin ADC) y
 *    R2 = 22 kΩ (pin ADC -> GND). Por lo tanto:
 *        Vbat = Vpin * (R1 + R2) / R2 = Vpin * 32 / 22.
 *  - Se usa GPIO26 (ADC0), libre en la Pico W (GPIO29 está ocupado por el Wi-Fi
 *    y el divisor interno VSYS/3).
 *
 * Referencias:
 *  - adc_read()/adc_select_input()/adc_gpio_init(): hardware_adc
 */

#include "battery_monitor.hpp"

#include "hardware/adc.h"

#include "pin_defs.hpp"

namespace battery {

// Número de muestras promediadas para reducir el ruido del ADC.
#define BATTERY_SAMPLES 16u

// -----------------------------------------------------------------------------
// Inicialización
// -----------------------------------------------------------------------------
void init() {
    // Inicializa el ADC y prepara GPIO26 (ADC0) como entrada analógica.
    adc_init();
    adc_gpio_init(VSYS_PIN);
}

// -----------------------------------------------------------------------------
// Lectura de tensión
// -----------------------------------------------------------------------------
uint32_t read_voltage_mv() {
    uint32_t accum = 0;

    adc_select_input(VSYS_ADC_CHANNEL);
    for (uint i = 0; i < BATTERY_SAMPLES; i++) {
        accum += adc_read();
    }

    uint32_t avg = accum / BATTERY_SAMPLES;

    // Tensión en el pin ADC: Vpin = avg * 3300 / 4095 (mV).
    uint32_t vpin_mv = (avg * 3300u) / 4095u;

    // Tensión de batería: Vbat = Vpin * (R1 + R2) / R2 = Vpin * 32 / 22.
    return (vpin_mv * (VSYS_R1_OHM + VSYS_R2_OHM)) / VSYS_R2_OHM;
}

// -----------------------------------------------------------------------------
// Conversión tensión -> porcentaje (curva LiPo 1S)
// -----------------------------------------------------------------------------
uint8_t voltage_to_percent(uint32_t voltage_mv) {
    // Puntos de la curva de descarga (1 celda LiPo/Li-Ion):
    //   4200 mV -> 100 %
    //   3700 mV ->  50 %
    //   3000 mV ->   0 %
    // Interpolación lineal por tramos.
    uint32_t v = voltage_mv;

    if (v >= 4200u) return 100u;
    if (v <= 3000u) return 0u;

    if (v >= 3700u) {
        // Tramo superior: 50% .. 100% entre 3700 y 4200 mV (500 mV de rango).
        uint32_t above = v - 3700u;                 // 0..500
        return (uint8_t)(50u + (above * 50u) / 500u);
    } else {
        // Tramo inferior: 0% .. 50% entre 3000 y 3700 mV (700 mV de rango).
        uint32_t above = v - 3000u;                 // 0..700
        return (uint8_t)((above * 50u) / 700u);
    }
}

bool is_low_battery(uint8_t percent) {
    return percent < 15u;
}

} // namespace battery
