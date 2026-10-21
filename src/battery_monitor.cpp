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

// Calibración activa de la curva de descarga (tabla por tramos) y de la
// compensación por carga. Se inicializa con la curva por defecto (4200/3700/3000
// mV, sin compensación) y puede reemplazarse desde la web (persistida en Flash).
static BatteryCalibration g_calib;

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
    uint32_t vbat_mv = (vpin_mv * (VSYS_R1_OHM + VSYS_R2_OHM)) / VSYS_R2_OHM;

    // Corrección por corriente estimada: con el consumo activo (radio/Wi-Fi) la
    // tensión medida cae por la resistencia interna (IR drop). `load_comp_mv`
    // compensa esa caída para estimar la tensión en reposo (0 = sin corrección).
    uint32_t comp = g_calib.load_comp_mv;
    if (comp > 500u) comp = 500u;   // límite de seguridad (evita sobre-corregir).
    return vbat_mv + comp;
}

// -----------------------------------------------------------------------------
// Conversión tensión -> porcentaje (curva LiPo 1S)
// -----------------------------------------------------------------------------
uint8_t voltage_to_percent(uint32_t voltage_mv) {
    // Puntos de la curva de descarga (1 celda LiPo/Li-Ion), configurables:
    //   v_full_mv -> 100 %,  v_mid_mv -> 50 %,  v_empty_mv -> 0 %.
    // Interpolación lineal por tramos. Con los valores por defecto (4200/3700/3000
    // mV) el resultado es idéntico al de la curva fija original.
    uint32_t v  = voltage_mv;
    uint32_t vf = g_calib.v_full_mv;
    uint32_t vm = g_calib.v_mid_mv;
    uint32_t ve = g_calib.v_empty_mv;

    // Guarda de integridad: si la calibración no cumple ve < vm < vf (p. ej. por
    // corrupción en Flash), se cae a la curva por defecto para no dividir por cero.
    if (!(vf > vm && vm > ve)) { vf = 4200u; vm = 3700u; ve = 3000u; }

    if (v >= vf) return 100u;
    if (v <= ve) return 0u;

    if (v >= vm) {
        // Tramo superior: 50% .. 100% entre vm y vf.
        return (uint8_t)(50u + ((v - vm) * 50u) / (vf - vm));
    }
    // Tramo inferior: 0% .. 50% entre ve y vm.
    return (uint8_t)(((v - ve) * 50u) / (vm - ve));
}

bool is_low_battery(uint8_t percent) {
    return percent < 15u;
}

void set_calibration(const BatteryCalibration& calib) {
    g_calib = calib;
    // Límite de seguridad de la compensación (el orden ve < vm < vf lo valida
    // voltage_to_percent() con su guarda interna).
    if (g_calib.load_comp_mv > 500u) g_calib.load_comp_mv = 500u;
}

BatteryCalibration calibration() {
    return g_calib;
}

} // namespace battery
