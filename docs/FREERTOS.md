# Análisis de FreeRTOS — PicoReflex

## 1. Qué es FreeRTOS

FreeRTOS es un **sistema operativo en tiempo real (RTOS)** de código abierto. En
la RP2040 (Pico W) se usa en modo **SMP** (Symmetric Multi-Processing): ambos
núcleos ejecutan el **planificador de FreeRTOS**, y las tareas se reparten entre
los dos núcleos de forma transparente.

## 2. Antes vs. después

| Aspecto | Antes (raw dual-core) | Después (FreeRTOS SMP) |
|---|---|---|
| Ejecución por núcleo | Fija: Core 0 = juego, Core 1 = red | Dinámica: el planificador reparte tareas |
| Arranque | `multicore_launch_core1()` | `vTaskStartScheduler()` |
| Sincronización | `critical_section` + `queue` + flags `volatile` | + semáforos/mutex de FreeRTOS |
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

## 4. Cambios realizados (v0.3.0)

- `CMakeLists.txt`: enlaza `pico_cyw43_arch_lwip_sys_freertos` y
  `FreeRTOS-Kernel-Heap4`; agrega `FreeRTOS_Kernel_import.cmake`.
- `include/FreeRTOSConfig.h`: configuración SMP (2 núcleos, heap 128 KB, tick 1 kHz).
- `lwipopts.h`: `NO_SYS 0`.
- `src/main.cpp`: crea `game_task` y `network_task` (semáforo binario de
  sincronización) y arranca el scheduler.
- `game_logic.cpp`, `web_server.cpp`, `audio.cpp`: `sleep_ms` → `vTaskDelay`.

## 5. Requisitos de compilación

FreeRTOS-Kernel no está incluido en el SDK. Hay que clonarlo:

```powershell
git clone --branch V11.1.0 https://github.com/FreeRTOS/FreeRTOS-Kernel.git %USERPROFILE%\.pico-sdk\FreeRTOS-Kernel
```

O definir `FREERTOS_KERNEL_PATH`. El CI ya lo clona automáticamente.

## 6. Pendiente de verificación en hardware

- **Asignación de núcleos**: la IRQ GPIO de botones se configura en el núcleo que
  ejecuta `game::init()`; con SMP el núcleo puede variar (funciona igual, pero
  verificar que los botones responden).
- **DORMANT**: el modo *dormant* (`low_power_dormant_until_gpio_pin_state`) asume
  ambos núcleos en WFE; con FreeRTOS SMP requiere rework (quedó con el *fallback*
  `__wfi()` y se recomienda desactivarlo o migrar a `vTaskSuspend` + sleep).
- **Escrituras a Flash**: `flash_safe_execute` usa `multicore_lockout`; con
  FreeRTOS SMP conviene verificar que no haya *race* entre la tarea de juego y la
  de red.
