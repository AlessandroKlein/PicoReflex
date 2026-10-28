/**
 * @file   main.cpp
 * @brief  Punto de entrada del PicoReflex con FreeRTOS (SMP, 2 núcleos).
 *
 * FreeRTOS SMP reemplaza al arranque manual por `multicore_launch_core1()`:
 *   - `game_task`: lógica del juego en tiempo real (IRQ GPIO, LEDs, buzzer PWM,
 *     Flash).
 *   - `network_task`: Wi-Fi (`cyw43_arch` sys_freertos), servidor HTTP (lwIP),
 *     display OLED, batería.
 *
 * PUNTO CLAVE: `cyw43_arch_init()` se ejecuta DENTRO de `network_task` (vía
 * `web::init()`), es decir DESPUÉS de `vTaskStartScheduler()`. Con
 * `pico_cyw43_arch_lwip_sys_freertos` (NO_SYS=0), esto crea la tarea `tcpip` de
 * lwIP. Es el patrón oficial de pico-examples (pico_w/wifi/freertos).
 *
 * Sincronización: un semáforo binario garantiza que `game::init()` (que detecta el
 * "forzar AP" por botón 0) termine antes de que la red intente conectarse.
 *
 * Referencias:
 *  - pico-examples pico_w/wifi/freertos (iperf/tcp): https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/freertos
 *  - pico_cyw43_arch_lwip_sys_freertos
 */

#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "config.hpp"
#include "game_logic.hpp"
#include "audio.hpp"
#include "web_server.hpp"

// Semáforo binario: game_task lo da tras init; network_task lo toma antes de red.
static SemaphoreHandle_t g_game_init_sem;

// Timeout del hardware watchdog (ms). Máximo en RP2040: 8388 ms (errata RP2040-E1).
// Se elige 8000 ms: mayor que cualquier operación bloqueante de un solo núcleo
// (p. ej. la conexión STA de 10 s en el Core 1 mientras el Core 0 sigue
// refrescando), y suficiente para reiniciar ante un cuelgue TOTAL de ambos núcleos.
#define WATCHDOG_TIMEOUT_MS 8000u

// -----------------------------------------------------------------------------
// Tarea del juego (equivale al antiguo Core 0)
// -----------------------------------------------------------------------------
static void game_task(void*) {
    printf("[game] init iniciando...\n");
    game::init();                       // GPIO (LEDs/botones), IRQ, DMA, PRNG, Flash.
    printf("[game] init OK\n");
    xSemaphoreGive(g_game_init_sem);    // Señaliza que el juego ya inicializó.
    game::run();                        // Bucle del juego (bloquea para siempre).
}

// -----------------------------------------------------------------------------
// Tarea de red (equivale al antiguo Core 1)
// -----------------------------------------------------------------------------
static void network_task(void*) {
    printf("[net] esperando init del juego...\n");
    // Espera a que el juego inicialice (para leer bien el flag "forzar AP").
    xSemaphoreTake(g_game_init_sem, portMAX_DELAY);
    printf("[net] iniciando red (cyw43_arch_init)...\n");
    web::init();                        // cyw43_arch_init() (sys_freertos) + batería.
    printf("[net] red inicializada\n");
    web::run();                         // Bucle de red/display (bloquea para siempre).
}

// -----------------------------------------------------------------------------
int main() {
    stdio_init_all();
    printf("\n[main] PicoReflex arrancando\n");

    // Diagnóstico de la causa del último reinicio ANTES de re-habilitar el
    // watchdog: watchdog_enable_caused_reboot() distingue un reinicio por timeout
    // del watchdog (habilitado en el arranque previo) de un arranque normal por
    // alimentación o botón RUN. watchdog_caused_reboot() confirma el origen.
    if (watchdog_caused_reboot()) {
        if (watchdog_enable_caused_reboot()) {
            printf("[main] Reinicio por timeout del hardware watchdog\n");
        } else {
            printf("[main] Reinicio por watchdog (force/reboot, no por timeout)\n");
        }
    }

    // Habilita el hardware watchdog: si NINGÚN núcleo llama watchdog_update()
    // dentro de WATCHDOG_TIMEOUT_MS, el chip se reinicia automáticamente.
    // pause_on_debug=true detiene el contador mientras hay un debugger conectado
    // (no falsea reinicios durante la depuración).
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

    shared_state_init();

    // Buzzer PWM (se configura una sola vez antes del scheduler).
    audio::init();

    g_game_init_sem = xSemaphoreCreateBinary();

    // Tareas: la de red con prioridad mayor y más stack (lwIP/display).
    // cyw43_arch_init() se hace DENTRO de network_task (patrón oficial pico-examples).
    xTaskCreate(game_task,    "game",    2048, nullptr, tskIDLE_PRIORITY + 2, nullptr);
    xTaskCreate(network_task, "network", 4096, nullptr, tskIDLE_PRIORITY + 3, nullptr);

    // Arranca el planificador (SMP en 2 núcleos). No retorna.
    vTaskStartScheduler();

    // Nunca debería llegar acá.
    return 0;
}
