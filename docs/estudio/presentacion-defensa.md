# Presentación de defensa — PicoReflex

Guía por filmina con los datos principales y qué decir en cada una.
Presentación: `Presentacion_Defensa_PicoReflex.pptx` (generada con
`tools/generate_presentacion_defensa.py`).

> Versión v0.9.7 · Pico SDK 2.3.0 · FreeRTOS SMP. Énfasis (~50 %): mecanismo de
> juego (estados + timing) y seed (xorshift32).
>
> **Diseño (Swiss Modernism 2.0):** paleta azul `#2563EB` + acento naranja
> `#F97316`, tipografía Inter, cards blancas sobre fondo `#F8FAFC`. Diagramas
> embebidos en las filminas **3** (diagrama de bloques), **5** (flujo del juego),
> **11** (flujo de red) y **12** (esquemático); la filmina **6** usa una tabla con
> cabecera coloreada y la **4** el árbol de archivos (monospace, claves en naranja).

---

## 1. Portada

- Título: **PicoReflex — Juego de reacción en Raspberry Pi Pico W**.
- Materia: Microprocesadores · Tecnicatura en Automatización y Robótica · UNL · 2026.
- Autor: Alessandro Klein · Versión v0.9.7.
- **Qué decir:** presentarse y adelantar el orden — qué es → arquitectura →
  mecanismo de juego → seed → Simón Dice → precisión → red/OTA → hardware.

## 2. ¿Qué es PicoReflex?

- Dispositivo de **entrenamiento de reflejos**: 5 LEDs + 5 pulsadores (mismo índice).
- Mide el **tiempo de reacción en microsegundos** (`time_us_32()` capturado en la
  IRQ del PIO).
- **4 modos**: libre, contrarreloj, muerte súbita, **Simón Dice**.
- **Seed determinista** (`xorshift32`) → secuencias reproducibles para competencias.
- Servidor web embebido (configuración en vivo), **OTA A/B**, Wi-Fi STA/AP.
- Hardware: OLED SSD1306/SH1106, buzzer PWM, medición de batería por ADC.

## 3. Arquitectura dual-core (FreeRTOS SMP)

- RP2040 (2 núcleos) + CYW43439 (Wi-Fi); **FreeRTOS SMP** sobre ambos núcleos.
- `game_task` (Core 0, stack 2048 B, prioridad IDLE+2): juego, IRQ, LEDs, Flash,
  watchdog.
- `network_task` (Core 1, stack 4096 B, prioridad IDLE+3): Wi-Fi, web, display,
  batería, watchdog.
- Comunicación entre tareas: `critical_section` (estado compartido) + `queue_t`
  (eventos Core1 → Core0).
- Arranque: watchdog → `shared_state_init()` → `game::init()` → `xTaskCreate` ×2 →
  `vTaskStartScheduler()`.

## 4. Estructura de archivos

- Árbol **espejo verbatim** de `docs/es/picoreflex/Arquitectura.md` §3 (aprobado).
- Raíz: `CMakeLists.txt` (build, `PICO_BOARD=pico_w`) + `lwipopts.h` (NO_SYS=0) +
  integración FreeRTOS + 2 programas PIO (`button_debounce.pio`, `ws2812.pio`).
- `include/` (14 headers) → `src/` (14 fuentes) → `docs/`.
- **En naranja (clave de la defensa):** `game_logic.cpp/.hpp` (motor + seed),
  `config.hpp` (estado compartido), `flash_storage.cpp/.hpp` (persistencia),
  `main.cpp` (arranque), `web_server.cpp` (red/OTA).
- `linker/pico_flash_region.ld` = mapa Flash OTA A/B; `bootloader.c` + `ota.c`
  cierran la OTA; `web_ui.cpp` tiene el frontend HTML/CSS embebido.
- **Qué decir:** recorrer de arriba hacia abajo y detenerse en los naranja.

## 5. Mecanismo de juego — máquina de estados

- `game::run()` (`src/game_logic.cpp:536-700`) **nunca retorna**.
- Estados: **IDLE → COUNTDOWN → RUNNING → EVALUACIÓN → IDLE** + rama SIMON_SAYS.
- Transiciones (línea exacta, `src/game_logic.cpp`):
  - IDLE → COUNTDOWN `:570-575` (cualquier botón, `g_button_pending`).
  - COUNTDOWN → RUNNING `:386-397` (cuenta regresiva `start_delay_s`).
  - RUNNING → EVALUACIÓN `:632-644` / `:648-651` (pulsación o timeout).
  - EVALUACIÓN → IDLE `:693-695` (muerte súbita + fallo → `end_round`).
  - Drenado de eventos web `:541-565` (START/STOP/CONFIG).

## 6. Mecanismo de juego — matemática del timing

- `delay_ms` **uniforme** en `[min, max]` (`:610-611`):
  `min + next_range(max − min + 1)` (el `+1` es el off-by-one del rango).
- `t0` capturado al encender el LED (`:620`).
- `reaction_us = g_button_ts_us − t0` (`:635`): **resta SIN signo → módulo 2³²**,
  absorbe el wrap del contador sin código especial.
- `tolerance_us` (`:623-626`): 1000 / 500 / 250 ms según dificultad; FREE_PLAY =
  `UINT32_MAX` (sin límite).
- TIMED: fin por `round_seconds` (`:654-655`).
- Timestamp capturado en la **IRQ del PIO** (`:167-185`), no en el bucle → jitter
  mínimo.

## 7. Seed — xorshift32 (el algoritmo)

- Marsaglia (2003), `src/game_logic.cpp:48-55`:
  ```c
  x ^= x << 13u;
  x ^= x >> 17u;
  x ^= x << 5u;
  ```
- **Linealidad sobre GF(2)**: XOR = suma módulo 2; cada shift es una matriz 32×32.
- Transición completa: **T = (I+L⁵)(I+R¹⁷)(I+L¹³)**.
- **Periodo máximo 2³²−1** (polinomio característico primitivo en GF(2)).
- No es criptográfico (lineal y predecible): sirve para jugabilidad, no para secretos.

## 8. Seed — propiedades matemáticas

- **Estado 0 = punto fijo** (`0<<k=0`, `0^0=0`): `set_seed(0)` lo reemplaza por
  `0x9E3779B9 = ⌊2³²/φ⌋` (razón áurea φ≈1.618, constante *nothing-up-my-sleeve*)
  — `include/game_logic.hpp:45`.
- **Sesgo del módulo** (`:58-61`): `next_range(n) = next() % n`. xorshift32 nunca
  emite 0, así que su salida en un periodo es {1,…,2³²−1}; para `n=5` (`LED_COUNT`),
  `2³²−1 = 5·858 993 459` → **sesgo exacto 0**.
- **Determinismo**: misma semilla → misma secuencia (partidas reproducibles).
- **Siembra** (`src/game_logic.cpp:406`): `seed=0` → `get_rand_32()` (aleatorio);
  `seed≠0` → determinista. Persistida en Flash (`:404`).

## 9. Simón Dice

- `start_round()` inicia con longitud 1 (`:409-411`).
- `simon_show_sequence` (`:465-474`) muestra **toda** la secuencia (LED + tono,
  velocidad acelera con la longitud).
- `simon_wait_input` (`:477-498`) valida elemento a elemento: **`btn == expected`**.
- Acertar todo → crece hasta `SIMON_MAX_SEQ 64` (`:522-528`).
- Fallar → `game_over` + reinicio a longitud 1 (`:511-519`).
- La secuencia es **determinista según la semilla** (misma semilla → misma secuencia).

## 10. Precisión y timing (por qué µs)

- `time_us_32()`: contador hardware de 1 µs, da la vuelta cada 2³² µs ≈ **71,58 min**.
- Timestamp tomado en la **IRQ del PIO** (no en el bucle) → jitter mínimo.
- La **resta sin signo absorbe el wrap** del contador sin código especial.
- **Debounce por PIO**: una state machine por botón, lockout ~5 ms, sin CPU ni
  latencia de IRQ.

## 11. Red + OTA

- Servidor web **raw lwIP** (`request[1024]`, `g_body[8192]`, `html_escape`).
- Wi-Fi **STA** + respaldo **AP**: reconexión en caliente (`STA_MAX_RETRIES 3`,
  backoff 1→15 s, delay de asentamiento 200 ms).
- **Captive portal** (DNS :53) + **DHCP** (DORA :67) + **mDNS** `picoreflex.local`
  (UDP 5353).
- **Auth PIN**: `WEB_PIN "1234"`, cookie `pr_auth`/Bearer, rate-limit 60 req/s +
  5 login/s.
- **OTA A/B**: bootloader `0x10000000` + app `0x10010000` + staging + CRC32 +
  rollback automático.

## 12. Hardware

- **Display OLED SSD1306/SH1106** (I2C1, 0x3C/0x3D, selección runtime persistida).
- **Debounce por PIO** (`button_debounce.pio`, 26 instrucciones, una SM por botón).
- **Buzzer PWM** (GPIO16, `f = clk_sys / (clkdiv·(wrap+1))`, duty 50 %).
- **Batería ADC** (GPIO26/ADC0, divisor 10 kΩ/22 kΩ, curva 4200/3700/3000 mV).
- **Watchdog hardware** 8000 ms (ambos núcleos refrescan).

## 13. Cierre — qué van a preguntar

- ¿Por qué resta sin signo? → módulo 2³² absorbe el wrap del contador.
- ¿Por qué 13/17/5? → periodo máximo (polinomio primitivo en GF(2)).
- ¿Semilla 0? → punto fijo; se reemplaza por 0x9E3779B9.
- ¿Reproducible? → misma semilla = misma secuencia de LEDs/Simón.
- **Lo más importante**: mecanismo de juego (estados + timing) y seed (xorshift32).
