/**
 * @file   battery_monitor.cpp
 * @brief  Implementación de la medición de batería por ADC (VSYS).
 *
 * Detalles técnicos:
 *  - ADC de la RP2040: 12 bits (0..4095), tensión de referencia = 3.3 V.
 *  - En la Pico W, GPIO29 (ADC3) está conectado a VSYS/3 (divisor 3:1 interno).
 *    Por lo tanto:  VSYS = 3 * (adc / 4095) * 3.3 V.
 *  - GPIO29 también es WL_CLK (SPI del CYW43439): la lectura se protege con
 *    cyw43_arch_lwip_begin()/end() (== cyw43_thread_enter/exit), tal como indica
 *    el board header boards/pico_w.h.
 *
 * Referencias:
 *  - adc_read()/adc_select_input(): hardware_adc
 *  - cyw43_arch_lwip_begin/end: pico_cyw43_arch
 */

#include "battery_monitor.hpp"

#include "hardware/adc.h"
#include "pico/cyw43_arch.h"

#include "pin_defs.hpp"

namespace battery {

// Número de muestras promediadas para reducir el ruido del ADC.
#define BATTERY_SAMPLES 16u

// -----------------------------------------------------------------------------
// Inicialización
// -----------------------------------------------------------------------------
void init() {
    // Inicializa el ADC (configura el reloj del ADC, ~500 kS/s).
    // NOTA: NO se llama adc_gpio_init(29): en la Pico W ese pin lo administra el
    // driver del CYW43 (es el SPI CLK). El ADC3 puede leerlo igualmente.
    adc_init();
}

// -----------------------------------------------------------------------------
// Lectura de tensión
// -----------------------------------------------------------------------------
uint32_t read_voltage_mv() {
    uint32_t accum = 0;

    // Protege la lectura: el GPIO29 es compartido con el SPI del Wi-Fi.
    cyw43_arch_lwip_begin();
    adc_select_input(VSYS_ADC_CHANNEL);
    for (uint i = 0; i < BATTERY_SAMPLES; i++) {
        accum += adc_read();
    }
    cyw43_arch_lwip_end();

    uint32_t avg = accum / BATTERY_SAMPLES;

    // VSYS = 3 * (avg / 4095) * 3300 mV. Se usa aritmética entera (mV).
    //   Vpin = avg * 3300 / 4095
    //   VSYS = 3 * Vpin
    uint32_t vpin_mv = (avg * 3300u) / 4095u;
    return vpin_mv * 3u;
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
