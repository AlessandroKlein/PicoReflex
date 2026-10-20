/**
 * @file   main.cpp
 * @brief  Punto de entrada del PicoReflex y arranque de los dos núcleos.
 *
 * División de tareas por núcleo (dual-core RP2040, ver docs/DECISIONES.md):
 *
 *   Core 0 (juego en tiempo real):
 *     - Lógica del juego, LEDs, interrupciones de pulsadores, PWM del buzzer.
 *     - Medición de reacción en microsegundos (hardware_timer).
 *     - Persistencia de récords en Flash (flash_safe_execute).
 *
 *   Core 1 (red + display):
 *     - Inicialización del CYW43 (cyw43_arch) y pila lwIP.
 *     - Servidor HTTP embebido (STA / SoftAP).
 *     - Medición de batería (ADC) y refresco del display OLED.
 *
 * Sincronización: estado compartido protegido con critical_section + queue
 * (ver config.hpp / shared_state.cpp).
 *
 * Referencias:
 *  - multicore_launch_core1: https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_multicore
 *  - flash_safe_execute_core_init: https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_flash
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"      // flash_safe_execute_core_init()

#include "config.hpp"
#include "game_logic.hpp"
#include "audio.hpp"
#include "web_server.hpp"

// -----------------------------------------------------------------------------
// Entrada del Core 1 (red + display)
// -----------------------------------------------------------------------------
static void core1_entry() {
    // Habilita a ESTE núcleo para ser "bloqueado" durante escrituras a Flash
    // iniciadas por el Core 0 (ver flash_safe_execute). Debe llamarse antes de
    // que el Core 0 intente guardar récords.
    flash_safe_execute_core_init();

    // Inicializa el chip Wi-Fi (cyw43_arch) y la medición de batería, y luego
    // entra al bucle principal del servidor web + display (no retorna).
    web::init();
    web::run();
}

// -----------------------------------------------------------------------------
// main() — Core 0
// -----------------------------------------------------------------------------
int main() {
    // Estándar: inicializa UART/USB para depuración (printf).
    stdio_init_all();

    // Estado compartido (critical_section + colas) antes de arrancar el Core 1.
    shared_state_init();

    // Habilita a este núcleo (Core 0) para ser bloqueado durante escrituras a
    // Flash iniciadas desde el Core 1 (por robustez/futuro).
    flash_safe_execute_core_init();

    // Inicialización del hardware del Core 0.
    audio::init();      // Buzzer PWM.
    game::init();       // GPIO (LEDs/pulsadores), IRQ, DMA, PRNG, carga de Flash.

    // Arranca el Core 1 (red + display).
    multicore_launch_core1(core1_entry);

    // Bucle principal del juego en el Core 0 (bloquea para siempre).
    game::run();

    return 0;
}
