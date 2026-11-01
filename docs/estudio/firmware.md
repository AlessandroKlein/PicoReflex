# Núcleo de firmware de PicoReflex — Material de estudio y defensa

> Guía para explicar y defender oralmente el núcleo de firmware del juego de
> reacción PicoReflex (Raspberry Pi Pico W, Pico SDK 2.3.0 + FreeRTOS SMP).
> Cada módulo sigue la estructura: **qué hace → cómo funciona (código) → por qué
> se eligió así → bugs/trampas → preguntas de defensa**.

Todas las referencias `archivo:línea` apuntan al código real del repositorio.

---

## 1. `main.cpp` — Arranque y creación de tareas

### (a) Qué hace
Es el punto de entrada. Configura `stdio`, diagnostica la causa del último
reinicio, habilita el **hardware watchdog**, inicializa el estado compartido y el
buzzer, crea **dos tareas FreeRTOS** (juego y red) con afinidad de núcleo fija, y
arranca el planificador SMP. No retorna.

### (b) Cómo funciona
Secuencia de `main()` (`src/main.cpp:72-118`):

1. `stdio_init_all()` (`src/main.cpp:73`) — USB CDC + UART0.
2. **Diagnóstico de reinicio** *antes* de re-habilitar el watchdog
   (`src/main.cpp:80-86`): `watchdog_caused_reboot()` confirma que el origen fue
   el watchdog; `watchdog_enable_caused_reboot()` distingue *timeout* de
   *force/reboot*.
3. `watchdog_enable(WATCHDOG_TIMEOUT_MS, true)` (`src/main.cpp:92`), con
   `WATCHDOG_TIMEOUT_MS = 8000u` (`src/main.cpp:45`).
4. `shared_state_init()` (`src/main.cpp:94`) y `audio::init()` (`src/main.cpp:97`).
5. `xSemaphoreCreateBinary()` (`src/main.cpp:99`): el semáforo binario
   `g_game_init_sem` ordena el arranque (el juego termina su `init()` antes de que
   la red intente conectar).
6. Creación de tareas con **afinidad fija** (`src/main.cpp:111-112`):

   | Tarea | Stack | Prioridad | Núcleo | Por qué |
   |---|---|---|---|---|
   | `game_task` → `game::run()` | 2048 B | `tskIDLE_PRIORITY + 2` | Core 0 | Lógica de tiempo real (IRQ, LEDs, Flash) |
   | `network_task` → `web::run()` | 4096 B | `tskIDLE_PRIORITY + 3` | Core 1 | lwIP/HTTP/display (más stack y prioridad) |

   `xTaskCreateAffinitySet(..., 1u << 0, ...)` fija el Core 0 y `1u << 1` el Core 1.
7. `vTaskStartScheduler()` (`src/main.cpp:115`) — arranca FreeRTOS SMP en ambos
   núcleos; no retorna.

`game_task` (`src/main.cpp:50-56`): `game::init()` → `xSemaphoreGive()` →
`game::run()` (bloquea para siempre). `network_task` (`src/main.cpp:61-69`):
`xSemaphoreTake(portMAX_DELAY)` → `web::init()` (aquí se llama
`cyw43_arch_init()`) → `web::run()`.

### (c) Por qué se eligió ese diseño
- **`cyw43_arch_init()` dentro de `network_task`**, no en `main()`: es el patrón
  oficial de `pico_w/wifi/freertos`; con `pico_cyw43_arch_lwip_sys_freertos`
  (`NO_SYS=0`) crea la tarea `tcpip` de lwIP después del scheduler
  (`src/main.cpp:11-14`, `docs/DECISIONES.md` §23).
- **Afinidad de núcleo fija obligatoria** para que `flash_safe_execute()` sea
  determinista: con `configTICK_CORE=0` el SysTick solo está en el Core 0; si la
  escritura a Flash cayera en el Core 1, el `__wfe()` interno no tendría tick
  periódico para re-evaluar el timeout y se colgaría (`src/main.cpp:104-110`).
- **Semáforo de arranque**: `game::init()` lee el flag "forzar AP" por botón 0
  antes de que la red intente conectarse (`src/main.cpp:16-17`).

### (d) Bugs/trampas
- **`cyw43_arch_init()` en `main()` antes del scheduler** rompió el arranque en el
  intento v0.3.0 (revertido) — `docs/DECISIONES.md` §23.
- **Stack del Core 1 insuficiente** (2 KB) causaba hard fault al reconectar/escaneo
  Wi-Fi → se subió a 4 KB (`docs/DECISIONES.md` §24).

### (e) Preguntas probables de defensa
1. **¿Por qué la tarea de red tiene más stack y más prioridad que la de juego?**
   Porque aloja lwIP + HTTP + display (necesita ~4 KB de stack y no puede demorarse),
   mientras que el juego es de tiempo real pero simple (2048 B le alcanza).
2. **¿Por qué se fija la afinidad de núcleo?** Para que la escritura a Flash
   (`flash_safe_execute`) siempre ocurra en el Core 0, donde el SysTick provee el
   wake-up periódico del `__wfe()` de la zona segura.
3. **¿Qué garantiza que el juego inicialice antes que la red?** El semáforo binario
   `g_game_init_sem`: `network_task` se bloquea en `xSemaphoreTake(portMAX_DELAY)`
   hasta que `game_task` hace `xSemaphoreGive`.
4. **¿Por qué se diagnostica el watchdog *antes* de re-habilitarlo?** Porque
   `watchdog_enable_caused_reboot()` solo es fiable si se lee antes de que un nuevo
   `watchdog_enable` borre el flag de causa.

---

## 2. `game_task` / `game_logic` — Motor del juego

### (a) Qué hace
Motor de reacción en el Core 0: configura LEDs/pulsadores (debounce por PIO),
mide la reacción en **microsegundos**, ejecuta la máquina de estados del juego
(4 modos), genera la secuencia pseudoaleatoria y actualiza estadísticas/persistencia.

### (b) Cómo funciona
- **Modos** (`include/config.hpp:80-93`): `FREE_PLAY=0` (sin penalización),
  `TIMED=1` (ronda de duración fija), `SUDDEN_DEATH=2` (un fallo termina),
  `SIMON_SAYS=3` (secuencia de memoria creciente).
- **Dificultad y tolerancia** (`include/config.hpp:98-103` + `:288-295`):
  `EASY=1000 ms`, `NORMAL=500 ms`, `HARD=250 ms` — `difficulty_tolerance_ms()` es
  un `switch` inline.
- **PRNG `xorshift32`** (`include/game_logic.hpp:39-65`): estado de 32 bits;
  `next()` hace `x ^= x<<13; x ^= x>>17; x ^= x<<5`; `next_range(n) = next() % n`.
  `set_seed(0)` usa la constante `0x9E3779B9u` (el estado todo-cero dejaría el
  generador atascado en 0).
- **Máquina de estados** en `run()` (`src/game_logic.cpp:536-700`):
  1. `watchdog_update()` cada vuelta (`:538`); drena eventos de control de la web
     (`:541-565`).
  2. **IDLE** (`:570-599`): cualquier pulsación (`g_button_pending`) dispara
     `start_with_countdown()`; si no, evalúa dormir o `vTaskDelay(5 ms)`.
  3. **SIMON_SAYS** (`:602-605` → `simon_says_round` `:501-531`).
  4. **RUNNING (reacción)** (`:607-668`): apaga LEDs, espera un intervalo aleatorio
     `delay_ms = stimulus_min_ms + g_rng.next_range(stimulus_max_ms - stimulus_min_ms + 1)`
     (`:610-611`), enciende un LED aleatorio (`:618-619`), captura `t0 = time_us_32()`
     y espera la pulsación o el timeout.
  5. **Evaluación** (`:675-696`): `stats_record()`, feedback de audio, récord,
     `SUDDEN_DEATH` termina la ronda.
- **Tiempo de reacción**: el timestamp se captura **dentro de la IRQ del PIO**
  (`src/game_logic.cpp:167-185`), con `time_us_32()` (resolución 1 µs); la reacción
  es `reaction_us = g_button_ts_us - t0` (`:635`), resta sin signo segura ante el
  *wrap* del contador de 32 bits (~71.6 min).
- **Tolerancia**: `tolerance_us = UINT32_MAX` en FREE_PLAY (sin timeout) o
  `difficulty_tolerance_ms * 1000` (`:623-626`).
- **Siembra** (`start_round`, `:402-416`): `g_rng.set_seed(cfg.seed != 0 ? cfg.seed : get_rand_32())`;
  en Simón inicializa `g_seq_length = 1` y `g_sequence[0] = g_rng.next_range(LED_COUNT)`.
- **Simón Dice** (`:426-531`): `simon_show_sequence()` presenta cada LED con su tono
  (escala pentatónica `:438-441`), la velocidad acelera con la longitud
  (`simon_show_interval_ms` reduce −4 % por elemento con suelo al 40 %);
  `simon_wait_input()` valida cada elemento con un timeout generoso
  (`tolerancia*6 + 2000 ms`).

### (c) Por qué se eligió ese diseño
- **`xorshift32` propio y no `rand()`/`pico_rand`**: `pico_rand` **no expone una
  función para fijar la semilla** (se auto-siembra con entropía del hardware), y
  para el modo "semilla determinista" hace falta una secuencia **idéntica en cada
  reinicio** (`include/game_logic.hpp:35-37`, `docs/DECISIONES.md` §3).
- **Timestamp en la IRQ**: minimiza latencia/jitter de la medición
  (`docs/DECISIONES.md` §4).
- **Debounce por PIO**: cada botón tiene una state machine que filtra el rebote en
  hardware; la CPU solo captura el timestamp (`src/game_logic.cpp:153-158`).

### (d) Bugs/trampas
- **Wrap de `span = smax - smin + 1`** si `smin > smax` → el rango se desbordaba.
  Se acotó en `web_server.cpp` (v0.1.3, `CHANGELOG.md:542-543`).
- **Disparador de inicio**: pasó de "dos botones simultáneos" (complejo y propenso a
  fallo) a **cualquier botón** (`docs/DECISIONES.md` §35, `src/game_logic.cpp:570-575`).

### (e) Preguntas probables de defensa
1. **¿Por qué no usar `rand()` o `pico_rand`?** `pico_rand` no permite fijar la
   semilla; para una secuencia repetible implementamos `xorshift32` (Marsaglia 2003),
   trivial y sin estado oculto.
2. **¿Cómo se mide el tiempo de reacción sin jitter?** El contador `time_us_32()`
   (TIMELR hardware, 1 µs) se lee dentro de la propia IRQ del flanco; la resta sin
   signo absorbe el wrap de 32 bits.
3. **¿Qué pasa si el jugador aprieta el botón equivocado?** `miss` → `stats_record`
   con `hit=false`; en `SUDDEN_DEATH` además `end_round()`.
4. **¿Cómo es determinista la secuencia de Simón?** Se siembra `xorshift32` con la
   semilla de `GameConfig`; la misma semilla produce la misma secuencia.

---

## 3. `shared_state` — Estado compartido entre núcleos

### (a) Qué hace
Provee acceso **thread-safe** al estado compartido entre Core 0 (juego) y Core 1
(red/display): configuración, estadísticas, estado del sistema, y una cola de
eventos de control asíncronos Core1→Core0.

### (b) Cómo funciona
- Objetos (`src/shared_state.cpp:22-26`): un `critical_section_t g_cs` protege
  `g_config`, `g_stats`, `g_status`; una `queue_t g_ctrl_q` lleva los eventos de
  control.
- `queue_init(&g_ctrl_q, sizeof(uint8_t), 16)` (`:44`): 16 slots de 1 byte.
- **Acceso a datos**: cada getter/setter copia bajo `critical_section_enter_blocking`
  / `exit` (p. ej. `config_get` `:50-56`, `stats_record` `:87-115`).
- **Eventos**: `control_send()` usa `queue_try_add` (**no bloquea**; si la cola está
  llena se descarta el evento, `:120-125`); `control_try_pop()` con `queue_try_remove`
  (`:127-134`).
- Flags de dormancia/persistencia como `volatile bool` (`:29-32`): accesos de 1 byte,
  atómicos en ARM.

### (c) Por qué se eligió ese diseño
- **`critical_section_t` en vez de mutex**: las secciones son muy cortas (copiar una
  struct), así que el *spinlock* es más simple y determinista
  (`src/shared_state.cpp:9-12`).
- **`queue_t` en vez de los FIFOs inter-núcleo crudos**: la propia documentación del
  SDK recomienda una cola en lugar de los FIFOs (`include/config.hpp:13-18`,
  `docs/DECISIONES.md` §6).

### (d) Bugs/trampas
- **Carrera de persistencia vs reconexión** (`g_persist_done`): escribir Flash
  (Core 0) y reconectar `cyw43_arch_deinit/init` (Core 1) a la vez crasheaba; se
  sincronizó con el flag `g_persist_done` (`CHANGELOG.md:359-362`).

### (e) Preguntas probables de defensa
1. **¿Por qué dos mecanismos (cola + critical_section)?** La cola es para eventos
   asíncronos (productor Core1 → consumidor Core0); el critical_section protege
   lecturas/escrituras síncronas de datos compartidos.
2. **¿Por qué no mutex?** Las secciones críticas son cortas; un spinlock es más
   simple y determinista (sin bloqueo por prioridad).
3. **¿Qué pasa si la cola se llena?** `queue_try_add` descarta el evento sin
   bloquear; el usuario puede reintentar desde la web (diseño aceptado).

---

## 4. `flash_storage` — Persistencia no volátil

### (a) Qué hace
Guarda/lee la configuración y los récords en el **último sector** de la Flash QSPI
de 2 MB (`0x1FF000`), fuera del firmware. También expone las escrituras seguras para
el OTA (imagen de staging + metadatos).

### (b) Cómo funciona
- **`PersistentData`** (`include/flash_storage.hpp:42-81`): `magic`, `version`,
  `user_name[16]`, `best_time_us`, `last_seed`, `ranking[5]` (top mejores tiempos),
  credenciales Wi-Fi/AP, `oled_enabled`, `start_delay_s`, `batt_calib`,
  `display_controller`, `sleep_enabled`/`idle_sleep_min`, y la config de juego
  (`game_mode`, `game_difficulty`, `seed`, `round_seconds`, `stimulus_min_ms`,
  `stimulus_max_ms`), y `checksum` al final.
- Constantes: `PERSISTENT_MAGIC = 0x5245464C` ("REFL" little-endian,
  `flash_storage.hpp:88`); `FLASH_TARGET_OFFSET = 0x200000 - FLASH_SECTOR_SIZE = 0x1FF000`
  (`:91`); `PERSISTENT_VERSION = 7` (`:36`); `static_assert(sizeof(PersistentData) <= FLASH_SECTOR_SIZE)`
  (`:84-85`).
- **`load()`** (`src/flash_storage.cpp:94-114`): lee por XIP; si `magic` no coincide
  (Flash virgen = todo `0xFF`) devuelve `false`; copia la struct; si `version` cambió
  → `false`; si no, recomputa el checksum y compara.
- **`update_checksum()`** (`:68-77`): suma simple de todos los bytes *antes* del campo
  `checksum`.
- **`save()`** (`:138-157`): copia a un buffer de 4 KB relleno con `0xFF`, redondea a
  múltiplo de `FLASH_PAGE_SIZE` (256 B) y ejecuta `flash_safe_execute(do_write, &job, 1000)`.
  `do_write` (`:130-134`) llama `flash_range_erase` (4096 B) + `flash_range_program`.
- **`flash_safe_execute`** (pico_flash) deshabilita IRQs (`save_and_disable_interrupts`)
  y bloquea al otro núcleo (`multicore_lockout`) para que no se ejecute código desde
  Flash durante el borrado/programación (`include/flash_storage.hpp:8-13`,
  `docs/DECISIONES.md` §10).

### (c) Por qué se eligió ese diseño
- **Último sector lejos del firmware** (que arranca en `0x10000000`) para no chocar
  con el código ni con los slots A/B de OTA (`include/ota.h:5-15`).
- **`flash_safe_execute`** encapsula exactamente la secuencia
  `multicore_lockout_start() + save_and_disable_interrupts()` que exige el hardware
  (`docs/DECISIONES.md` §10).
- **`PERSISTENT_VERSION`**: al cambiar la estructura se incrementa; `load()` descarta
  los datos viejos y re-inicializa a defaults en el primer arranque (migración segura).

### (d) Bugs/trampas
- **Subir `PERSISTENT_VERSION` borra la config persistida**: cada bump (v2…v7) resetea
  Wi-Fi/récords/ranking en el primer arranque. Debe avisarse en el CHANGELOG
  (p. ej. `CHANGELOG.md:71-73`).
- **Escritura innecesaria al ver una página** (v0.9.5): visitar `/display` sin enviar
  formulario disparaba un guardado (crash); se corrigió para persistir solo al enviar
  (`CHANGELOG.md:36-38`).
- **Reinicio al guardar en modo AP** (v0.9.5): `flash_safe_execute` en el Core 1 bajo
  SMP se colgaba (`__wfe()` sin tick) → watchdog; se fijó la escritura al Core 0
  (`CHANGELOG.md:39-44`).

### (e) Preguntas probables de defensa
1. **¿Qué pasa si subo `PERSISTENT_VERSION`?** `load()` detecta `version` distinto,
   devuelve `false`, y el firmware re-inicializa `PersistentData` con defaults
   (pérdida de config/récords previos).
2. **¿Cómo se detecta corrupción?** Magic (`0x5245464C`) + versión + checksum (suma
   de bytes previos); si algo falla, se descartan los datos.
3. **¿Por qué no se puede escribir Flash desde ambos núcleos a la vez?** El borrado/
   programación apaga el XIP; `flash_safe_execute` bloquea al otro núcleo y las IRQ.

---

## 5. `watchdog` — Hardware watchdog y reinicio automático

### (a) Qué hace
Reinicia automáticamente la Pico si **ningún núcleo** llama `watchdog_update()`
dentro del timeout (8 s), evitando que un cuelgue total deje el dispositivo muerto.

### (b) Cómo funciona
- Habilitación: `watchdog_enable(8000, true)` en `main()` (`src/main.cpp:92`),
  con `pause_on_debug=true` (el contador se detiene con debugger conectado).
- Timeout `8000 ms`: el máximo del RP2040 es `8388 ms` (errata RP2040-E1)
  (`src/main.cpp:41-44`).
- **Refresco** (`watchdog_update()`) en:
  - el bucle principal del juego, cada vuelta (`src/game_logic.cpp:538`);
  - la cuenta regresiva (`:390`, `:394`);
  - la presentación larga de la secuencia de Simón (`:468`);
  - la espera de pulsación de Simón (`:495`);
  - la espera de reacción (FREE_PLAY puede ser indefinida, `:667`).
- **Diagnóstico de la causa** *antes* de re-habilitar: `watchdog_caused_reboot()`
  confirma origen watchdog; `watchdog_enable_caused_reboot()` distingue timeout de
  force/reboot (`src/main.cpp:80-86`).

### (c) Por qué se eligió ese diseño
- **8 s > cualquier operación bloqueante de un solo núcleo**: la conexión STA (10 s)
  ocurre en el Core 1 mientras el Core 0 sigue refrescando; y basta para reiniciar
  ante un cuelgue **total** de ambos núcleos (`src/main.cpp:42-44`).

### (d) Bugs/trampas
- **Watchdog encubriendo cuelgues de Flash**: si la escritura a Flash se hacía en el
  Core 1 (sin tick), el watchdog (8 s) reiniciaba a mitad del borrado → Flash a medio
  borrar → checksum inválido → reset de fábrica (`CHANGELOG.md:39-44`). El refresco
  correcto no sustituye a la escritura correcta.

### (e) Preguntas probables de defensa
1. **¿Por qué 8 s y no más?** Es el margen máximo razonable bajo 8388 ms (errata
   RP2040-E1) y supera cualquier bloqueo de un solo núcleo.
2. **¿Quién refresca el watchdog?** Ambos núcleos: el bucle del juego y los bucles
   de espera; si ambos se cuelgan, nadie refresca y el chip se reinicia.
3. **¿Cómo sé si el último arranque fue por watchdog?** `watchdog_enable_caused_reboot()`
   leído antes de `watchdog_enable`.

---

## 6. `bootloader` — Segundo nivel y OTA A/B

### (a) Qué hace
Bootloader de **segundo nivel** (en `0x10000000`, primeros 64 KB) que arranca la
aplicación enlazada en el slot de aplicación (`0x10010000`). Implementa **OTA A/B con
staging + commit atómico + rollback**: si hay un commit pendiente, verifica el CRC del
staging, lo copia al slot de aplicación y salta; si el staging está corrupto, arranca
la imagen anterior.

### (b) Cómo funciona
- **Mapa de Flash** (`include/ota.h:5-15`): bootloader `0x10000000` (64 KB), app
  `0x10010000` (896 KB), staging `0x100F0000` (896 KB), metadatos OTA `0x101E0000`
  (4 KB), persistentes `0x1FF000` (4 KB).
- **`main()`** (`src/bootloader.c:108-178`): `stdio_uart_init()` (solo UART, sin USB);
  lee metadatos; si `commit_state == OTA_COMMIT_PENDING` verifica staging →
  `flash_copy()` + verifica copia → `ota_meta_write_raw()` → `jump_to_app()`; si
  staging corrupto → rollback (limpia commit) → `jump_to_app()`; sin commit →
  verifica imagen comprometida → salta o entra en USB.
- **`jump_to_app()`** (`src/bootloader.c:42-79`): lee `vt[0]` (MSP) y `vt[1]` (reset)
  en `XIP_BASE + OTA_APP_OFFSET + OTA_SLOT_VECTOR` (`0x10010100`); valida
  `msp` en RAM y `reset` en Flash (`:51-52`); si es inválido → `reset_usb_boot(0,0)`.
  Antes de saltar **replica un reset limpio** (`:67-76`):
  1. `nvic_hw->icer = 0xFFFFFFFF` (deshabilita IRQ externas 0..31);
  2. `nvic_hw->icpr = 0xFFFFFFFF` (limpia pendientes);
  3. `systick_hw->csr = 0` (apaga SysTick);
  4. `scb_hw->vtor = vt_addr`;
  5. `msr msp, vt[0]` + `cpsie i` (PRIMASK=0) + `dsb`/`isb` + `bx vt[1]`.

### (c) Por qué se eligió ese diseño
- **A/B + staging** permite actualizar sin arriesgar la imagen en uso: la nueva se
  descarga y verifica en staging, y solo se conmuta si el CRC es correcto
  (`include/ota.h:17-25`).
- **`flash_copy` sector a sector con buffer de 4 KB** evita usar `flash_safe_execute`
  (el bootloader es mononúcleo, sin FreeRTOS) (`src/bootloader.c:84-103`).
- **Bootloader UART-only**: el USB lo reserva la app, que es quien necesita el
  terminal (`src/bootloader.c:108-113`).

### (d) Bugs/trampas
- **Off-by-one del MSP** (v0.8.1): la validación `msp < 0x20042000u` rechazaba el
  valor legítimo `__StackTop = 0x20042000` (tope exacto de los 264 KB de SRAM), por
  lo que el bootloader descartaba la imagen y volvía a modo bootloader. La cota
  correcta es `msp <= 0x20042000u` (`src/bootloader.c:51`, `CHANGELOG.md:181-185`).
  **Por qué importa**: un `__StackTop` igual al tope de la RAM es un valor válido y
  común; rechazarlo dejaba la placa sin arrancar aunque la app fuera correcta.
- **"USB no reconocido" (Code 43)** (v0.9.0): `jump_to_app()` saltaba con `cpsid i`
  (PRIMASK=1); la app no podía atender la IRQ USB (donde corre `tud_task` de TinyUSB)
  durante `stdio_init_all()`, así Windows veía el attach pero nunca el descriptor
  válido. Fix: limpiar NVIC/SysTick y saltar con `cpsie i` (`docs/DECISIONES.md` §31,
  `CHANGELOG.md:153-155`).

### (e) Preguntas probables de defensa
1. **¿Por qué `msp <= 0x20042000` y no `<`?** Porque `0x20042000` es el tope exacto
   de la SRAM de 264 KB (fin de `SCRATCH_Y`); es un `__StackTop` válido y rechazarlo
   (off-by-one) impedía arrancar.
2. **¿Qué garantiza que no se ejecute código corrupto?** La validación de MSP (RAM) +
   PC (Flash) antes de saltar, y el CRC32 del staging antes de conmutar.
3. **¿Por qué `cpsie i` y no `cpsid i` antes del salto?** Para replicar un reset
   limpio (PRIMASK=0): si las IRQ quedan enmascaradas, la IRQ USB no puede disparar
   y el host no reconoce el dispositivo.
4. **¿Qué hace el rollback?** Si el staging está corrupto, se limpia el commit y se
   arranca la imagen anterior (que sigue intacta en el slot de aplicación).

---

## Referencias cruzadas

- `docs/DECISIONES.md` — §1–§36 (decisiones citadas en cada módulo).
- `CHANGELOG.md` — historial de fixes (watchdog v0.7.0, bootloader v0.8.1/v0.9.0,
  persistencia v0.9.5, etc.).
- `include/ota.h` — mapa de particiones y metadatos OTA.
