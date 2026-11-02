# Guía de Defensa — PicoReflex

> Material consolidado para estudiar y defender oralmente el trabajo final.
> Integra las secciones de los especialistas (`firmware.md`, `red.md`,
> `hardware.md`), la documentación del proyecto y el código real. Cada afirmación
> técnica está respaldada por `archivo:línea` del repositorio.

- **Proyecto:** PicoReflex — juego de reacción sobre Raspberry Pi Pico W.
- **Materia:** Microprocesadores — Tecnicatura en Automatización y Robótica · Universidad Nacional del Litoral.
- **Autor:** Alessandro Klein.
- **Versión del firmware:** v0.9.7.

---

## Resumen ejecutivo

PicoReflex es un juego de reacción (entrenamiento de reflejos) sobre una
**Raspberry Pi Pico W** (RP2040 + radio CYW43439), programado en **C/C++ con el
Pico SDK oficial 2.3.0** y **FreeRTOS SMP**. Enciende de forma pseudoaleatoria uno
de cinco LEDs y mide en **microsegundos** el tiempo de reacción del jugador al
pulsar el botón asociado. Ofrece **4 modos** (libre, contrarreloj, muerte súbita,
Simón Dice) y **3 dificultades** (tolerancia 1000/500/250 ms), con una semilla
determinista o aleatoria. Todo se configura y supervisa desde un **servidor web
embebido** (lwIP, Wi-Fi STA/AP) con **autenticación por PIN**, y se actualiza por
aire con **OTA A/B y rollback**. El hardware incluye display OLED
(SSD1306/SH1106), buzzer PWM, medición de batería por ADC y debounce de pulsadores
por **PIO**.

---

## 1. Mapa de archivos

### Raíz

| Archivo | Propósito y relación |
|---|---|
| `CMakeLists.txt` | Build principal: define `PICO_BOARD pico_w`, la versión única `PICOREFLEX_VERSION "0.9.7"` (línea 85), enlaza `pico_cyw43_arch_lwip_sys_freertos` + `pico_lwip_mdns` + `FreeRTOS-Kernel-Heap4`, y el linker OTA (`pico_flash_region.ld`). |
| `lwipopts.h` | Configuración de lwIP: `NO_SYS 0`, `TCPIP_THREAD_STACKSIZE 4096` (fix del cuelgue), mDNS/IGMP, `MEMP_NUM_SYS_TIMEOUT 32`. |
| `include/FreeRTOSConfig.h` | Configuración de FreeRTOS SMP (2 núcleos, heap_4, `configTICK_CORE 0`). |
| `button_debounce.pio` | Programa PIO del debounce de pulsadores (26 instrucciones, una SM por botón). |
| `ws2812.pio` | Programa PIO para los LEDs WS2812/NeoPixel opcionales. |
| `linker/pico_flash_region.ld` | Script de linker que enlaza la app en el slot de aplicación `0x10010000`. |
| `pico_sdk_import.cmake` / `FreeRTOS_Kernel_import.cmake` | Importadores del SDK y del kernel FreeRTOS. |
| `README.md` / `SECURITY.md` / `CHANGELOG.md` | Documentación de portada, seguridad y historial de versiones. |

### `src/` — código fuente (14 archivos)

| Archivo | Propósito |
|---|---|
| `main.cpp` | Punto de entrada: diagnostica reinicio, habilita watchdog, crea `game_task` y `network_task` con afinidad fija, arranca el scheduler. |
| `game_logic.cpp` | Motor del juego en el Core 0: máquina de estados (4 modos), PRNG `xorshift32`, medición µs, debounce PIO, persistencia, dormancia. |
| `shared_state.cpp` | Estado compartido thread-safe: `critical_section_t` (config/stats/status) + `queue_t` de eventos Core1→Core0. |
| `flash_storage.cpp` | Persistencia en el último sector Flash (`0x1FF000`) con `flash_safe_execute`; escrituras seguras para OTA. |
| `web_server.cpp` | Backend HTTP (raw lwIP), máquina STA/AP + reconexión, mDNS, OTA, auth PIN + rate-limit, bucle `web::run()`. |
| `web_ui.cpp` | Frontend HTML/CSS/JS: rutas → páginas, `html_escape()` anti-XSS, "Comprobar actualizaciones". |
| `captive_portal.cpp` | Servidor DNS mínimo (puerto 53) que redirige toda consulta a `192.168.4.1`. |
| `dhcpserver.c` | Servidor DHCP del AP (diálogo DORA, puerto 67), restringido al netif del AP. |
| `display_driver.cpp` | Driver OLED SSD1306/SH1106 por I2C1, framebuffer, self-test `0xA5`. |
| `audio.cpp` | Buzzer pasivo por PWM (tonos/melodías de feedback). |
| `battery_monitor.cpp` | ADC + divisor, conversión mV → % con curva de descarga calibrable. |
| `led_driver.cpp` | LEDs GPIO o WS2812 (según `PICOREFLEX_WS2812`). |
| `ota.c` | CRC32 (IEEE 802.3) + metadatos + verificación, compartido app/bootloader. |
| `bootloader.c` | Bootloader de segundo nivel: staging → commit atómico / rollback → `jump_to_app()`. |

### `include/` — headers (15 archivos)

| Header | Propósito |
|---|---|
| `config.hpp` | `GameConfig`, `GameStats`, `SystemStatus`, modos/dificultad, calibración de batería, eventos de control. |
| `pin_defs.hpp` | Única fuente de verdad de pines (LEDs 2-6, botones 7-11, I2C 14/15, buzzer 16, batería 26). |
| `ota.h` | Mapa de particiones Flash + `ota_metadata_t` + `commit_state`. |
| `flash_storage.hpp` | `PersistentData` + `PERSISTENT_VERSION 7` + `ap_password_default`. |
| `web_server.hpp` / `web_ui.hpp` | APIs de la red/servidor web y del frontend. |
| `display_driver.hpp` | `Controller {SSD1306, SH1106}` + `init/show/self_test`. |
| `game_logic.hpp` | `xorshift32`, API del motor (`run`, `start_round`, Simón). |
| `audio.hpp` / `led_driver.hpp` | API del buzzer PWM y de los LEDs. |
| `battery_monitor.hpp` | API del ADC + `BatteryCalibration`. |
| `captive_portal.hpp` / `dhcpserver.h` | APIs del DNS y del DHCP del AP. |
| `font5x7.hpp` | Fuente bitmap 5×7 generada (para el OLED). |

### `docs/` — documentación

| Archivo | Propósito |
|---|---|
| `Idea.md` | Consigna original del trabajo. |
| `ARQUITECTURA.md` | Arquitectura general + mapa de Flash/consumo. |
| `DECISIONES.md` | Bitácora de decisiones (§1–§36), referencia cruzada por todos los módulos. |
| `FREERTOS.md` | Integración de FreeRTOS SMP. |
| `ESQUEMATICO.md` | Esquema de conexiones y mapa de pines completo. |
| `COMPILAR.md` | Build, flasheo y opciones de compilación. |
| `API.md` | Endpoints HTTP y parámetros. |
| `SEGURIDAD.md` | Auditoría de seguridad (hallazgos y mitigaciones). |
| `ROADMAP.md` / `CHANGELOG.md` / `DUDAS.md` / `REFERENCIAS.md` / `INFORME.md` | Hoja de ruta, historial, dudas, referencias e informe. |
| `estudio/firmware.md` | Sección de estudio: núcleo de firmware (t49). |
| `estudio/red.md` | Sección de estudio: red y web (t50). |
| `estudio/hardware.md` | Sección de estudio: hardware y periféricos (t51). |
| `img/` | Diagramas SVG (bloques, flujos Core 0/Core 1, esquemático, pinout) + PNG. |

### `tools/`

| Herramienta | Propósito |
|---|---|
| `generate_informe.py` | Genera `Informe_PicoReflex.docx` con python-docx. |
| `generate_guia_defensa.py` | Genera `Guia_de_Defensa_PicoReflex.docx` (esta guía). |
| `gen_font.py` | Regenera `font5x7.hpp`. |
| `generate_linker_script.py` | Genera `linker/pico_flash_region.ld`. |
| `size_report.cmake` | Reporte de tamaño (target `size`). |
| `web_preview/` | Vista previa estática del HTML embebido (sin hardware). |

---

## 2. Arquitectura general

![Diagrama de bloques](../img/Diagrama%20de%20bloques.svg)

PicoReflex corre sobre un **RP2040 dual-core** (2 núcleos ARM Cortex-M0+ a
125 MHz) con **FreeRTOS SMP**: el scheduler reparte las tareas entre los dos
núcleos, pero el firmware **fija la afinidad** de cada tarea a un núcleo concreto
(`xTaskCreateAffinitySet`, `src/main.cpp:111-112`):

| Tarea | Núcleo | Stack | Prioridad | Responsabilidad |
|---|---|---|---|---|
| `game_task` → `game::run()` | Core 0 | 2048 B | `IDLE+2` | Juego en tiempo real (IRQ, LEDs, Flash) |
| `network_task` → `web::run()` | Core 1 | 4096 B | `IDLE+3` | Wi-Fi + lwIP/HTTP + display + batería |

Además, lwIP (`NO_SYS=0`) corre en una **tarea `tcpip` propia** creada por
`cyw43_arch_init()` dentro de `network_task`.

**Modelo de memoria/compartición.** Los dos núcleos comparten datos a través de
`shared_state`:
- **`critical_section_t`** protege `GameConfig`, `GameStats` y `SystemStatus`
  (secciones cortas: copiar una struct).
- **`queue_t`** transporta los **eventos de control** asíncronos del Core 1 al
  Core 0 (16 slots, `queue_try_add` no bloquea).
- Flags `volatile bool` para dormancia/persistencia (accesos de 1 byte, atómicos).

**Ciclo de vida** (boot → init → scheduler):

1. **Boot ROM → boot2** → salta al **bootloader de segundo nivel** (`0x10000000`),
   que conmuta/verifica el slot de aplicación y salta a la app (`0x10010000`).
2. **`main()`** (`src/main.cpp:72-118`): `stdio_init_all()`, diagnóstico de la
   causa del reinicio, `watchdog_enable(8000 ms)`, `shared_state_init()`,
   `audio::init()`, crea las dos tareas con afinidad y `vTaskStartScheduler()`.
3. **`game_task`**: `game::init()` (configura LEDs/botones/display) → da el
   semáforo `g_game_init_sem` → `game::run()` (bucle que nunca retorna).
4. **`network_task`**: espera el semáforo (para que el juego lea el flag "forzar
   AP" antes) → `web::init()` (aquí `cyw43_arch_init()` crea la tarea `tcpip`) →
   `web::run()`.

**Por qué afinidad fija:** `flash_safe_execute()` (escritura a Flash) debe ocurrir
siempre en el Core 0, donde `configTICK_CORE=0` deja el SysTick; en el Core 1 el
`__wfe()` interno no tendría tick periódico y se colgaría (`src/main.cpp:104-110`).

---

## 3. Módulos

### 3.1 Núcleo de firmware

*(Detalle completo en `estudio/firmware.md`.)*

- **Arranque y tareas (`main.cpp`)** — crea `game_task`/`network_task` con
  afinidad fija; `cyw43_arch_init()` vive **dentro de `network_task`**, no en
  `main()` (patrón oficial `pico_w/wifi/freertos`).
- **Motor del juego (`game_logic.cpp`)** — máquina de estados con 4 modos
  (`FREE_PLAY`/`TIMED`/`SUDDEN_DEATH`/`SIMON_SAYS`), PRNG **`xorshift32`** (semilla
  determinista), y **medición de reacción en µs**: `time_us_32()` se lee **dentro
  de la IRQ del PIO**, no en el bucle. Detalle completo en los apartados
  **§3.1.1** (lógica de juego) y **§3.1.2** (el "seed").
- **Estado compartido (`shared_state.cpp`)** — `critical_section` (datos) +
  `queue_t` (eventos). Se eligió spinlock en vez de mutex por la brevedad de las
  secciones críticas.
- **Persistencia (`flash_storage.cpp`)** — último sector `0x1FF000`, magic
  `0x5245464C` ("REFL") + versión `PERSISTENT_VERSION 7` + checksum;
  `flash_safe_execute()` serializa el borrado/programación.
- **Watchdog (hardware)** — `watchdog_enable(8000 ms)`; ambos núcleos refrescan
  (`watchdog_update()`); diagnóstico de causa antes de re-habilitar.
- **Bootloader (`bootloader.c`)** — salto tipo-reset (`VTOR`, MSP, `cpsie i`) y
  OTA A/B con staging + commit atómico + rollback.

#### 3.1.1 Lógica de juego — máquina de estados y matemática del timing

Motor de reacción en el **Core 0**: el bucle `game::run()`
(`src/game_logic.cpp:536-700`) **nunca retorna**, ejecutando una máquina de estados
cíclica **IDLE → COUNTDOWN → RUNNING → EVALUACIÓN → IDLE** y una rama especial para
**Simón Dice** (`:602-605` → `simon_says_round` `:501-531`).

| Transición | Línea | Disparador |
|---|---|---|
| IDLE → COUNTDOWN | `:570-575` | cualquier botón (`g_button_pending`) |
| COUNTDOWN → RUNNING | `:386-397` | `start_with_countdown()` cuenta `start_delay_s` s y llama `start_round()` |
| RUNNING → EVALUACIÓN | `:632-644` / `:648-651` | pulsación (`hit`/`miss`/`timeout`) o timeout de tolerancia |
| EVALUACIÓN → IDLE | `:693-695` | `SUDDEN_DEATH` + fallo → `end_round()` (`:418-424`); resto → bucle arriba |

**Correspondencia pulsador ↔ LED (mismo índice):** en RUNNING se enciende un LED
objetivo y el botón correcto es el del **mismo índice** (`btn == expected`,
`src/game_logic.cpp:489`). LED 1↔P1 (GPIO2↔GPIO7), LED 2↔P2 (GPIO3↔GPIO8),
LED 3↔P3 (GPIO4↔GPIO9), LED 4↔P4 (GPIO5↔GPIO10), LED 5↔P5 (GPIO6↔GPIO11) —
`LED_PINS`/`BUTTON_PINS` en `include/pin_defs.hpp:36` y `:43`.

**Rama Simón Dice:** `start_round()` inicia la secuencia con longitud 1
(`:409-411`); `simon_show_sequence` (`:465-474`) muestra toda la secuencia;
`simon_wait_input` (`:477-498`) valida elemento a elemento; al acertar todo crece
hasta `SIMON_MAX_SEQ 64` (`:522-528`); al fallar → `game_over` + reinicio a
longitud 1 (`:511-519`).

**Matemática del timing** (todo sobre `time_us_32()`, contador hardware de 1 µs que
da la vuelta cada 2³² µs ≈ **71,58 min**):

- **Intervalo entre estímulos uniforme** (`:610-611`):
  `delay_ms = stimulus_min_ms + next_range(stimulus_max_ms − stimulus_min_ms + 1)`.
  El `+1` es el clásico off-by-one de un rango `[min, max]` (hay `max−min+1` valores).
- **`t0` se captura al encender el LED** (`:620`); **`reaction_us = g_button_ts_us − t0`**
  (`:635`) es una **resta SIN signo** → aritmética **módulo 2³²** que **absorbe el
  *wrap*** del contador sin ningún código especial (la reacción humana ≪ 71,58 min).
- **Tolerancia** (`:623-626`): `difficulty_tolerance_ms()·1000` = 1000/500/250 ms;
  en `FREE_PLAY` es `UINT32_MAX` (sin límite de tiempo).
- **Fin por tiempo en `TIMED`** (`:654-655`): `(time_us_32() − g_round_start_us) > round_seconds·1e6`.
- **El timestamp se captura DENTRO de la IRQ del PIO** (`:167-185`), no en el bucle,
  para minimizar latencia/jitter (`g_button_ts_us` se lee después sin polling).

#### 3.1.2 El "seed" — algoritmo matemático del PRNG `xorshift32`

Genera la secuencia pseudoaleatoria **determinista** que decide el orden de los LEDs
y la secuencia de Simón: **una misma semilla produce exactamente la misma secuencia**
(partidas reproducibles, récords comparables). Definición (`include/game_logic.hpp:48-55`):

```c
uint32_t next() {
    uint32_t x = state_;
    x ^= x << 13u;   // :50
    x ^= x >> 17u;   // :51
    x ^= x << 5u;    // :52
    state_ = x;
    return x;
}
```

- **Terna (13, 17, 5)**: publicada por Marsaglia (2003); da **periodo máximo
  2³²−1** (el polinomio característico de la transición es primitivo en GF(2)).
- **Linealidad sobre GF(2)**: XOR = suma módulo 2, y cada *shift* es una matriz
  32×32. La transición completa es un único mapa lineal `x' = T·x` con
  `T = (I+L⁵)(I+R¹⁷)(I+L¹³)` (orden = orden de ejecución).
- **Invertibilidad → permutación**: cada factor `(I+Lᵏ)` es invertible porque `Lᵏ`
  es **nilpotente** (`L³²=0`, luego `(Lᵏ)ᵐ=0` para `km≥32`); `T` es una **biyección**
  de los 2³² estados.
- **Periodo 2³²−1**: todos los estados no nulos forman **un único ciclo** de
  longitud 4 294 967 295.
- **Estado 0 = punto fijo** (`0<<k=0`, `0^0=0`): `set_seed(0)` lo sustituye por
  **`0x9E3779B9 = ⌊2³²/φ⌋`**, con φ=(1+√5)/2 ≈ 1.618 (razón áurea): constante
  "nothing-up-my-sleeve" con buen mezclado de bits (`include/game_logic.hpp:45`).
- **Sesgo del módulo** (`next_range(n) = next() % n`, `:58-61`): xorshift32 **nunca
  emite 0**, así que en un periodo completo su salida es `{1,…,2³²−1}` (2³²−1
  valores). Para `n=5` (`LED_COUNT`), `2³²−1 = 5·858 993 459` → **sesgo exacto 0**.
  Aun en el peor caso sería 1/2³² ≈ 2,3·10⁻¹⁰, despreciable (no hace falta
  *rejection sampling* para `n` pequeño).
- **Siembra** (`src/game_logic.cpp:406`):
  `g_rng.set_seed(cfg.seed != 0 ? cfg.seed : get_rand_32())` — `seed=0` =
  **aleatorio** (entropía hardware `pico_rand`), cualquier otro valor =
  **determinista**.
- **Persistencia** (`:404`): `g_persist.last_seed = cfg.seed` se guarda en Flash.
- **No es criptográfico**: xorshift32 es lineal y **predecible**; sirve solo para
  jugabilidad, no para secretos (no confundir con un CSPRNG).

### 3.2 Red y web

*(Detalle completo en `estudio/red.md`.)*

![Flujo de red (Core 1)](../img/Flujo%20de%20red%20(Core%201).svg)

- **Wi-Fi STA/AP** — intenta STA (`PicoW-ReactionGame`); si falla tras 4 intentos
  de 10 s, degrada a **SoftAP** (`192.168.4.1`). **Reconexión en caliente**
  (`sta_link_watchdog`, cada 1 s) con backoff exponencial (1→15 s) y degradación a
  AP tras `STA_MAX_RETRIES`. LED onboard: fijo=STA, parpadeo=AP.
- **lwIP (`NO_SYS=0`, `sys_freertos`)** — la raw API corre en la tarea `tcpip`;
  las llamadas desde `network_task` se protegen con `cyw43_arch_lwip_begin/end`
  (= `LOCK_TCPIP_CORE`).
- **Captive portal + DHCP** — `dhcpserver.c` (DORA, puerto 67, `udp_bind_netif`
  al AP) + `captive_portal.cpp` (DNS → `192.168.4.1`, AAAA→NODATA) + respuestas a
  los "connectivity check" de Android/iOS/Windows.
- **mDNS `picoreflex.local`** — UDP 5353, multicast `224.0.0.251` (`LWIP_IGMP 1`),
  servicio `_http._tcp`; solo en STA.
- **Servidor web (raw lwIP)** — `http_recv()` es el router (`request[1024]`,
  `g_body[8192]`); frontend separado en `web_ui.cpp` con `html_escape()` anti-XSS.
- **OTA A/B** — `POST /ota/upload` graba a staging con CRC32 en caliente; el
  bootloader conmuta solo si el CRC es correcto (rollback si falla). "Comprobar
  actualizaciones" consulta GitHub desde el **navegador** (la Pico no tiene TLS).
- **Auth PIN + rate-limit** — `WEB_PIN "1234"`, cookie `pr_auth` (FNV-1a + sal),
  comparación en tiempo constante, rate-limit global (60 req/s) y de login (5/s).

### 3.3 Hardware y periféricos

*(Detalle completo en `estudio/hardware.md`.)*

![Flujo del juego (Core 0)](../img/Flujo%20del%20juego%20(Core%200).svg)

- **Pinout (`pin_defs.hpp`)** — LEDs 2-6, botones 7-11, OLED I2C1 (14/15, 400 kHz),
  buzzer 16, batería GPIO26/ADC0 (divisor 10k/22k).
- **Display OLED SSD1306/SH1106** — 128×64 I2C, dos controladores seleccionables
  en runtime (`/display`) y persistidos; **cross charge-pump** (`0x8D,0x14` +
  `0xAD,0x8B`); SH1106 usa addressing por página + offset de columna +2; self-test
  `0xA5`.
- **Debounce por PIO** — una state machine por botón (`wait 0 pin 0` → `push` →
  `irq set 0` + lockout ~5 ms); la CPU solo captura `time_us_32()`.
- **Buzzer pasivo (PWM)** — `f = clk_sys / (clkdiv·(wrap+1))`, duty 50 %, driver a
  transistor.
- **Batería (ADC)** — 16 muestras promediadas, `Vbat = Vpin·32/22`, curva de
  descarga calibrable (4200/3700/3000 mV), alerta < 15 %.

![Esquemático](../img/Esquem%C3%A1tico.svg)

### 3.4 Seguridad

*(Detalle completo en `docs/SEGURIDAD.md`.)*

- **Autenticación PIN** en todos los endpoints de control/configuración
  (`/start /stop /reset /seed /game /display /wifi /battery /config /ota`).
- **Rate-limiting** anti fuerza bruta (ventana fija 1 s: 60 req/s global, 5 login/s).
- **XSS mitigado** (`html_escape()` en `web_ui.cpp`).
- **Credenciales AP/Wi-Fi** configurables y persistidas en Flash (sin clave fija).
- **Flash sin cifrar + checksum no criptográfico**: aceptado para datos de baja
  sensibilidad; se documenta como riesgo residual.
- **Sin TLS** (HTTP plano): asumido para LAN de confianza; la consulta a la API de
  GitHub la hace el navegador por HTTPS.

---

## 4. FAQ de defensa (20 preguntas)

1. **¿Por qué la tarea de red tiene más stack y más prioridad que la de juego?**
   Porque aloja lwIP + HTTP + display (necesita ~4 KB de stack y no puede demorarse);
   el juego es de tiempo real pero simple (2048 B le alcanza).

2. **¿Por qué se fija la afinidad de núcleo?**
   Para que `flash_safe_execute()` siempre ocurra en el Core 0, donde el SysTick
   (`configTICK_CORE=0`) provee el wake-up periódico del `__wfe()` de la zona segura.

3. **¿Por qué `cyw43_arch_init()` se llama dentro de `network_task` y no en `main()`?**
   Es el patrón oficial de `pico_w/wifi/freertos`: con `sys_freertos` crea la tarea
   `tcpip`, y eso debe pasar después de `vTaskStartScheduler()`.

4. **¿Qué es la tarea `tcpip` y cuál fue el bug del cuelgue en `cyw43_arch_init()`?**
   Es el hilo interno de lwIP (`NO_SYS=0`) donde corren las callbacks. El cuelgue se
   debía a que `lwipopts.h` no definía `TCPIP_THREAD_STACKSIZE` (default 0) →
   `xTaskCreate(0)` fallaba → `xSemaphoreTake(init_sem)` bloqueaba para siempre.
   Fix: `TCPIP_THREAD_STACKSIZE 4096`.

5. **¿Cómo se mide el tiempo de reacción en microsegundos sin jitter?**
   `time_us_32()` (timer hardware, 1 µs) se lee **dentro de la IRQ del PIO**, lo más
   cerca del flanco; la resta sin signo absorbe el wrap de 32 bits (~71.6 min).

6. **¿Por qué `xorshift32` y no `rand()`/`pico_rand`?**
   `pico_rand` no permite fijar la semilla (se auto-siembra); para una secuencia
   determinista y repetible implementamos `xorshift32` (Marsaglia 2003), trivial y
   sin estado oculto.

7. **¿Por qué dos mecanismos de comunicación (cola + critical_section)?**
   La cola transporta **eventos asíncronos** (Core1 → Core0); el `critical_section`
   protege lecturas/escrituras **síncronas** de datos compartidos. Se usa spinlock
   (no mutex) porque las secciones son muy cortas.

8. **¿Qué pasa si se incrementa `PERSISTENT_VERSION`?**
   `load()` detecta versión distinta, devuelve `false`, y el firmware re-inicializa
   `PersistentData` a defaults (se pierde config/récords previos en el primer arranque).

9. **¿Por qué el watchdog es de 8 s y quién lo refresca?**
   8 s es el margen máximo razonable bajo 8388 ms (errata RP2040-E1) y supera
   cualquier bloqueo de un solo núcleo. Lo refrescan **ambos núcleos**; si ambos se
   cuelgan, nadie refresca y el chip se reinicia.

10. **¿Qué hace `jump_to_app()` y por qué `cpsie i` y no `cpsid i`?**
    Salta del bootloader a la app replicando un reset limpio (NVIC/SysTick/VTOR, MSP).
    Debe usar `cpsie i` (IRQ habilitadas) para que la IRQ USB pueda disparar y el
    host reconozca el dispositivo; con `cpsid i` quedaba "USB no reconocido".

11. **¿Por qué `cyw43_arch_lwip_begin/end` equivale a `LOCK_TCPIP_CORE`?**
    Con `PICO_LWIP_CUSTOM_LOCK_TCPIP_CORE=1`, el lock del core lwIP se redirige al
    `async_context` del CYW43; `cyw43_arch_lwip_begin()` adquiere ese mismo
    `async_context` (mismo mutex recursivo).

12. **¿Por qué el delay de 200 ms en la reconexión AP→STA?**
    Porque `disable_ap_mode()`/`disable_sta_mode()` encolan el "link down" de forma
    **asíncrona** en el firmware CYW43; hay que dejar que cierre el modo anterior
    antes de lanzar el `join` STA.

13. **¿Por qué hace falta un DHCP propio + captive portal en el AP?**
    El CYW43 no trae servidor DHCP (`CYW43_NETUTILS` off): sin DHCP el cliente se
    asocia pero no recibe IP ("conecta y se desconecta"). El captive portal (DNS →
    `192.168.4.1`) redirige el navegador a la web embebida.

14. **¿Qué puerto/protocolo usa mDNS y por qué `LWIP_IGMP=1`?**
    UDP 5353, multicast a `224.0.0.251`. `LWIP_IGMP 1` permite a lwIP unirse
    (`igmp_joingroup`) al grupo multicast que usa mDNS.

15. **¿Por qué la raw API de lwIP y no sockets?**
    Menor footprint de RAM (`LWIP_SOCKET 0` / `LWIP_NETCONN 0`); con `NO_SYS=0` las
    callbacks corren en la tarea `tcpip`, y el modelo es determinista para un MCU.

16. **¿Cómo funciona el OTA A/B y por qué staging + rollback?**
    La web sube el `.bin` a un **slot de staging** y verifica CRC32; al reiniciar, el
    bootloader copia la imagen al slot de aplicación **solo si el CRC es correcto**,
    con **rollback** a la imagen anterior si falla. Así un corte de luz o un CRC
    incorrecto nunca deja el dispositivo "bricked".

17. **¿Cuál era el bug de `/login` y cómo funciona la autenticación?**
    Un `GET /login` sin `?pin=` devolvía `401` en vez del formulario (bucle: 302 →
    `/login` → 401). Fix: solo validar el PIN si el parámetro está presente. La auth
    usa PIN `WEB_PIN`, cookie `pr_auth` (FNV-1a + sal), comparación en tiempo
    constante y rate-limit.

18. **¿Por qué la batería se mide por GPIO26/ADC0 y no por GPIO29/VSYS?**
    En la Pico W, GPIO29 está tomado por el SPI del CYW43 (`WL_CLK`) y por el divisor
    interno VSYS/3; GPIO26 (ADC0) está libre y usa un divisor externo 10k/22k.

19. **¿Por qué el cross charge-pump (mandar `0x8D,0x14` y `0xAD,0x8B`)?**
    El ACK de I2C no distingue SSD1306 de SH1106. `0x8D,0x14` enciende el
    charge-pump del SSD1306 y `0xAD,0x8B` el DC-DC del SH1106; mandando ambos, el
    chip presente usa el suyo y el otro es no-op → init robusto ante selección errónea.

20. **¿Por qué debounce por PIO y no por software en la IRQ?**
    El PIO filtra en hardware de forma determinista, sin consumir CPU ni latencia de
    IRQ; la CPU solo captura `time_us_32()`. El timestamp se toma en el **primer**
    flanco (`wait 0 pin 0` → `irq set 0`), conservando la resolución de 1 µs.

21. **¿Por qué la terna 13, 17 y 5 en el xorshift32?**
    Es una de las ternas publicadas por Marsaglia (2003) que da **periodo máximo
    2³²−1**: el polinomio característico de `T` es primitivo en GF(2), así que todos
    los estados no nulos forman un único ciclo. No es un número mágico arbitrario.

22. **¿Qué periodo tiene el xorshift32 y por qué?**
    `2³²−1` = 4 294 967 295. Porque `T` es una **permutación** (invertible) cuyo
    único punto fijo es el 0, de modo que el ciclo de los estados no nulos tiene la
    longitud máxima posible para 32 bits.

23. **¿Qué pasa si la semilla es 0?**
    El estado 0 es **punto fijo** (`0<<k=0`, `0^0=0`), y `next()` devolvería 0 para
    siempre (generador "atascado"). Por eso `set_seed(0)` la reemplaza por
    `0x9E3779B9 = ⌊2³²/φ⌋` (`include/game_logic.hpp:45`).

24. **¿Cómo se garantiza que dos partidas con la misma semilla sean idénticas?**
    `set_seed(cfg.seed)` fija exactamente el estado inicial y `T` es determinista:
    la misma semilla produce la misma secuencia de `next()` → la misma secuencia de
    LEDs/Simón. En PicoReflex `seed=0` significa "aleatorio" (`get_rand_32()`, entropía
    hardware) y cualquier otro valor "determinista/reproducible" (`src/game_logic.cpp:406`).

---

## 5. Glosario técnico

- **FreeRTOS** — kernel de tiempo real (RTOS) que planifica tareas con prioridades.
- **SMP** — *Symmetric MultiProcessing*: el scheduler ejecuta las tareas en varios
  núcleos indistintamente (aquí 2 núcleos Cortex-M0+).
- **Tarea** — unidad de ejecución de FreeRTOS con su propio stack y prioridad.
- **Afinidad de núcleo** — restricción que fija una tarea a un núcleo concreto
  (`xTaskCreateAffinitySet`).
- **lwIP** — pila TCP/IP ligera para embebidos.
- **cyw43** — driver/abstracción del chip inalámbrico CYW43439 (Wi-Fi) de la Pico W.
- **`NO_SYS`** — opción de lwIP: `0` = con hilo `tcpip` (sys_freertos); `1` = raw
  API sin sistema operativo.
- **Captive portal** — técnica que redirige al navegador a una página local (DNS →
  IP propia) para capturar la conexión en un AP.
- **DHCP** — *Dynamic Host Configuration Protocol*: asigna IP a los clientes.
- **DORA** — secuencia DHCP: Discover → Offer → Request → Ack.
- **mDNS** — *Multicast DNS* (RFC 6762): resuelve `nombre.local` por multicast
  (UDP 5353), sin servidor DNS.
- **OTA** — *Over-The-Air*: actualización de firmware por la red.
- **A/B** — esquema de dos imágenes (slot activo + staging) para actualizar con
  rollback seguro.
- **I2C** — bus serie de dos hilos (SDA/SCL) para periféricos.
- **ADC** — *Analog-to-Digital Converter*: convierte tensión a número digital.
- **PWM** — *Pulse Width Modulation*: señal cuadrada con duty cycle ajustable.
- **PIO** — *Programmable I/O*: co-procesador del RP2040 con state machines para
  I/O determinista.
- **Flash / XIP** — memoria no volátil; XIP = ejecutar código directamente desde ella.
- **CRC32** — *Cyclic Redundancy Check* de 32 bits para detectar corrupción.
- **xorshift32** — PRNG de Marsaglia (2003): tres xor-shift (`x ^= x<<13; x ^= x>>17;
  x ^= x<<5`), periodo máximo 2³²−1, lineal sobre GF(2). La semilla 0 se reemplaza
  por `0x9E3779B9`.
- **GF(2)** — el campo de dos elementos {0,1} con XOR como suma y AND como producto;
  es el espacio donde viven los PRNG lineales bit a bit.
- **razón áurea (`0x9E3779B9`)** — `⌊2³²/φ⌋` con `φ=(1+√5)/2 ≈ 1.618`: constante
  "nothing-up-my-sleeve" usada para sembrar (estado inicial no nulo con buen mezclado
  de bits).
- **periodo (de un PRNG)** — cantidad de estados distintos antes de repetirse;
  xorshift32 con la terna (13,17,5) alcanza el máximo 2³²−1.
- **siembra determinista** — fijar la semilla para que la secuencia sea reproducible;
  en PicoReflex `seed=0` = aleatorio (`get_rand_32()`), `seed≠0` = determinista.
- **critical_section** — región protegida por spinlock para acceso exclusivo corto.
- **queue** — cola de mensajes para comunicación entre tareas/núcleos.
- **semáforo** — primitiva de sincronización (aquí, `g_game_init_sem` ordena el
  arranque).
- **watchdog** — temporizador hardware que reinicia si nadie lo refresca a tiempo.
