/**
 * @file   game_logic.hpp
 * @brief  Motor del juego de reacción (PRNG, máquina de estados, temporización).
 *
 * Este módulo se ejecuta en el **Core 0** y es responsable de:
 *  - Configurar los 5 LEDs (salidas) y los 5 pulsadores (entradas con pull-up
 *    e interrupción por flanco de bajada).
 *  - Generar la secuencia pseudoaleatoria de LEDs (determinista por semilla).
 *  - Medir el tiempo de reacción con resolución de microsegundos.
 *  - Actualizar las estadísticas (vía shared_state) y disparar el feedback sonoro.
 *
 * Temporización (decisión clave, ver docs/DECISIONES.md):
 *  - El timestamp de microsegundos lo provee el temporizador hardware
 *    `time_us_32()` (hardware/timer.h), capturado DENTRO de la interrupción GPIO
 *    para minimizar la latencia y el jitter.
 *  - La resta sin signo `t_final - t_inicial` maneja el "wrap" del contador de
 *    32 bits (~71.6 minutos) sin código especial.
 *
 * Referencias:
 *  - https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_timer
 *  - https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio
 */

#ifndef PICO_REFLEX_GAME_LOGIC_HPP
#define PICO_REFLEX_GAME_LOGIC_HPP

#include "pico/stdlib.h"
#include "config.hpp"

namespace game {

// =============================================================================
// PRNG determinista (xorshift32)
// =============================================================================
// pico_rand (pico/rand.h) NO permite fijar una semilla explícita (se auto-siembra
// con entropía), por lo que para el modo "semilla determinista" implementamos un
// generador xorshift32 propio, trivial y sin estado oculto.
// Referencia del algoritmo: Marsaglia, "Xorshift RNGs" (2003).
class XorShift32 {
public:
    explicit XorShift32(uint32_t seed = 0x9E3779B9u) { set_seed(seed); }

    // Fija la semilla. Si es 0 se usa una constante no nula (un estado todo-cero
    // dejaría el xorshift atascado en 0 para siempre).
    void set_seed(uint32_t s) { state_ = (s != 0u) ? s : 0x9E3779B9u; }

    // Devuelve el siguiente valor de 32 bits.
    uint32_t next() {
        uint32_t x = state_;
        x ^= x << 13u;
        x ^= x >> 17u;
        x ^= x << 5u;
        state_ = x;
        return x;
    }

    // Devuelve un valor uniforme en [0, n). Requiere n > 0.
    uint32_t next_range(uint32_t n) {
        // El módulo introduce un sesgo despreciable para n pequeños (n=5 aquí).
        return next() % n;
    }

private:
    uint32_t state_;
};

// =============================================================================
// API del motor
// =============================================================================

// Inicializa GPIO (LEDs y pulsadores), interrupciones y el PRNG.
// Debe llamarse UNA vez desde el Core 0 antes de run().
void init();

// Bucle principal del juego. Bloquea el Core 0 para siempre.
void run();

// Estado interno (expuesto para el display/web cuando sea necesario).
bool is_round_active();

} // namespace game

#endif // PICO_REFLEX_GAME_LOGIC_HPP
