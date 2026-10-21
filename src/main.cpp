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
 * PUNTO CLAVE: `cyw43_arch_init()` se ejecuta en `main()` **ANTES** de
 * `vTaskStartScheduler()`. Con `pico_cyw43_arch_lwip_sys_freertos`, esto crea la
 * tarea `tcpip` de lwIP; si se llamara dentro de una tarea (después del scheduler)
 * el Wi-Fi no arranca.
 *
 * Sincronización: un semáforo binario garantiza que `game::init()` (que detecta el
 * "forzar AP" por botón 0) termine antes de que la red intente conectarse.
 *
 * Referencias:
 *  - pico-examples pico_w/wifi/freertos (iperf/tcp): https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/freertos
 *  - pico_cyw43_arch_lwip_sys_freertos
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "pico/stdlib.h"

#include "config.hpp"
#include "game_logic.hpp"
#include "audio.hpp"
#include "web_server.hpp"

// Semáforo binario: game_task lo da tras init; network_task lo toma antes de red.
static SemaphoreHandle_t g_game_init_sem;

// -----------------------------------------------------------------------------
// Tarea del juego (equivale al antiguo Core 0)
// -----------------------------------------------------------------------------
static void game_task(void*) {
    game::init();                       // GPIO (LEDs/botones), IRQ, DMA, PRNG, Flash.
    xSemaphoreGive(g_game_init_sem);    // Señaliza que el juego ya inicializó.
    game::run();                        // Bucle del juego (bloquea para siempre).
}

// -----------------------------------------------------------------------------
// Tarea de red (equivale al antiguo Core 1)
// -----------------------------------------------------------------------------
static void network_task(void*) {
    // Espera a que el juego inicialice (para leer bien el flag "forzar AP").
    xSemaphoreTake(g_game_init_sem, portMAX_DELAY);
    web::init();                        // cyw43_arch_init() (sys_freertos) + batería.
    web::run();                         // Bucle de red/display (bloquea para siempre).
}

// -----------------------------------------------------------------------------
int main() {
    stdio_init_all();
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
