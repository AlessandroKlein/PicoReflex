# FreeRTOS — PicoReflex

> **Estado**: **INTEGRADO** en v0.5.0 (SMP, 2 núcleos).
> Historial: el primer intento (v0.3.0) falló en hardware y se revirtió en v0.3.1.
> La causa **no** fue la ubicación de `cyw43_arch_init()` (que, según los ejemplos
> oficiales, va **dentro de una tarea**, no en `main()`), sino el stack reducido y
> detalles de configuración. Con el patrón oficial corregido y el stack ampliado,
> se re-integró.

## 1. Qué es FreeRTOS

FreeRTOS es un **sistema operativo en tiempo real (RTOS)** de código abierto. En
la RP2040 (Pico W) se usa en modo **SMP** (Symmetric Multi-Processing): ambos
núcleos ejecutan el **planificador de FreeRTOS** y las tareas se reparten entre
los dos núcleos de forma transparente.

## 2. Antes vs. después

| Aspecto | Antes (raw dual-core) | Después (FreeRTOS SMP) |
|---|---|---|
| Ejecución por núcleo | Fija: Core 0 = juego, Core 1 = red | Fija por afinidad: Core 0 = juego, Core 1 = red (ver §7) |
| Arranque | `multicore_launch_core1()` | `vTaskStartScheduler()` |
| Sincronización | `critical_section` + `queue` + flags `volatile` | + semáforos de FreeRTOS |
| Wi-Fi/lwIP | `threadsafe_background` (IRQ baja) | `sys_freertos` (tarea `tcpip`) |
| Retardos | `sleep_ms` (bloquea el núcleo en WFE) | `vTaskDelay` (cede el núcleo) |
| lwIP | `NO_SYS=1` | `NO_SYS=0` |

## 3. Beneficios

- **Planificación real**: cada tarea cede el núcleo al bloquearse (sin WFE que
  congele el otro trabajo del núcleo).
- **Wi-Fi más robusto**: `pico_cyw43_arch_lwip_sys_freertos` atiende lwIP en una
  tarea dedicada (`tcpip`), con mejor prioridad que una IRQ de baja prioridad.
- **Escalable**: agregar tareas (p. ej. una para logueo, otra para OTA) es trivial.
- **Estándar de la industria**: aporta valor técnico al TP.

## 4. Cambios realizados (v0.5.0)

- `CMakeLists.txt`: enlaza `pico_cyw43_arch_lwip_sys_freertos` y
  `FreeRTOS-Kernel-Heap4`; agrega `FreeRTOS_Kernel_import.cmake` y
  `PICO_CORE1_STACK_SIZE=0x1000` (el port FreeRTOS arranca el Core 1 con
  `multicore_launch_core1`).
- `include/FreeRTOSConfig.h`: configuración SMP (2 núcleos, heap 128 KB, tick 1 kHz).
- `lwipopts.h`: `NO_SYS 0`.
- `src/main.cpp`: crea `game_task` y `network_task` (semáforo binario de
  sincronización) y arranca el scheduler.
- `src/web_server.cpp`: `cyw43_arch_init()` **dentro de la tarea de red**
  (`web::init()`), reconexión "liviana" (sin deinit/init) y `vTaskDelay`.
- `game_logic.cpp`, `web_server.cpp`, `audio.cpp`: `sleep_ms` → `vTaskDelay`.

## 5. Patrón oficial (corrección del primer intento)

En los ejemplos oficiales (`pico_w/wifi/freertos`), `cyw43_arch_init()` se llama
**dentro de `main_task`** (una tarea FreeRTOS creada antes del scheduler), NO en
`main()`:

```c
void main_task(void *params) {
    if (cyw43_arch_init()) { /* error */ }
    cyw43_arch_enable_sta_mode();
    ...
    while (true) vTaskDelay(100);
}
void vLaunch(void) {
    xTaskCreate(main_task, "TestMainThread", configMINIMAL_STACK_SIZE, NULL, TEST_TASK_PRIORITY, &task);
    vTaskStartScheduler();
}
```

PicoReflex sigue este patrón: `cyw43_arch_init()` está en `web::init()`, invocado
desde `network_task`.

## 6. Requisitos de compilación

FreeRTOS-Kernel no está incluido en el SDK. Hay que clonarlo:

```powershell
git clone --branch V11.1.0 https://github.com/FreeRTOS/FreeRTOS-Kernel.git %USERPROFILE%\.pico-sdk\FreeRTOS-Kernel
```

O definir `FREERTOS_KERNEL_PATH`. El CI lo clona automáticamente.

## 7. Limitaciones conocidas (con FreeRTOS SMP)

- **Afinidad de núcleo obligatoria para escribir Flash** (v0.9.x). Con
  `configTICK_CORE=0` el SysTick solo está en el Core 0. `flash_safe_execute()`
  (guardado de config / OTA) hace `__wfe()` + `time_reached(timeout)` en el
  núcleo que escribe; si ese núcleo es el Core 1, el `__wfe()` puede no
  despertarse jamás (sin tick periódico) y el **hardware watchdog reinicia**.
  Por eso `main.cpp` fija `game_task`→Core 0 y `network_task`→Core 1 con
  `xTaskCreateAffinitySet()`. Si se agregan más tareas que escriban Flash, deben
  fijarse también al Core 0 (o serializar la escritura).
- **DORMANT degradado**: el modo *dormant* real (`low_power_dormant_until_gpio_pin_state`)
  exige ambos núcleos en WFE y no es compatible con FreeRTOS SMP sin rework. Se
  reemplazó por un **sleep ligero** (`vTaskDelay`); el ahorro de energía profundo
  (< 2 mA) queda pendiente con la API `low_power` + FreeRTOS SMP.
- **Reconexión "liviana"**: al cambiar credenciales Wi-Fi no se hace
  `cyw43_arch_deinit/init` (la tarea `tcpip` no se recrea); se apaga el modo
  actual y se re-ejecuta STA/AP.

## 8. Referencias

- Ejemplos FreeRTOS: <https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/freertos>
- `pico_cyw43_arch_lwip_sys_freertos`: <https://www.raspberrypi.com/documentation/pico-sdk/networking.html>
- FreeRTOS-Kernel (V11.1.0): <https://github.com/FreeRTOS/FreeRTOS-Kernel>
