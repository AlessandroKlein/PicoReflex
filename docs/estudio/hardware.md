# Hardware y periféricos — PicoReflex

> Material de estudio para defender oralmente el trabajo final. Cada módulo sigue
> la misma estructura: **(a)** qué hace, **(b)** cómo funciona (con referencia real
> `archivo:línea`), **(c)** por qué ese diseño, **(d)** bugs/trampas y su fix,
> **(e)** preguntas probables de defensa con respuesta corta.
>
> Fuente de verdad de pines: [`include/pin_defs.hpp`](../pin_defs.hpp).

---

## 1. Pinout (mapa de pines)

### (a) Qué hace

Concentra en un único header **toda** la asignación de pines de la Raspberry Pi
Pico W, de modo que el resto del firmware nunca hardcodea un número de GPIO:
lee las macros `LED_PINS`, `BUTTON_PINS`, `I2C_DISPLAY_*`, `BUZZER_PIN`, `VSYS_PIN`.

### (b) Cómo funciona (código)

| Periférico | Pin | Macro | Referencia |
|---|---|---|---|
| LED 1..5 | GPIO 2..6 | `LED_PINS {2,3,4,5,6}` | `include/pin_defs.hpp:35-36` |
| Pulsador 1..5 | GPIO 7..11 | `BUTTON_PINS {7,8,9,10,11}` | `include/pin_defs.hpp:43` |
| OLED SDA / SCL | GPIO 14 / 15 | `I2C_DISPLAY i2c1`, `I2C_DISPLAY_SDA 14`, `I2C_DISPLAY_SCL 15` | `include/pin_defs.hpp:56-59` |
| Buzzer pasivo | GPIO 16 | `BUZZER_PIN 16u` | `include/pin_defs.hpp:76` |
| Batería (ADC0) | GPIO 26 | `VSYS_PIN 26u`, `VSYS_R1_OHM 10000`, `VSYS_R2_OHM 22000` | `include/pin_defs.hpp:87-90` |
| LED onboard Wi-Fi | CYW43 | `CYW43_WL_GPIO_LED_PIN` | `include/pin_defs.hpp:98-100` |

**Correspondencia pulsador ↔ LED (mismo índice):**

| LED | GPIO | Pulsador | GPIO |
|-----|------|----------|------|
| 1 (idx 0) | GPIO2 | 1 (idx 0) | GPIO7 |
| 2 (idx 1) | GPIO3 | 2 (idx 1) | GPIO8 |
| 3 (idx 2) | GPIO4 | 3 (idx 2) | GPIO9 |
| 4 (idx 3) | GPIO5 | 4 (idx 3) | GPIO10 |
| 5 (idx 4) | GPIO6 | 5 (idx 4) | GPIO11 |

- El botón correcto es el del **mismo índice** que el LED encendido:
  `g_led_pins[i] = LED_PINS` y `g_button_pins[i] = BUTTON_PINS`
  (`src/game_logic.cpp:62-63`), y el motor valida `btn == expected` (`:489`).

- La velocidad I2C se fija a **400 kHz** (fast mode): `#define I2C_BAUDRATE 400u * 1000u` — `include/pin_defs.hpp:59`.
- La dirección del OLED se define en 7 bits (`0x3C`/`0x3D`), con la aclaración de que
  los fabricantes la publican en 8 bits (`0x78`/`0x7A`): `include/pin_defs.hpp:61-66`.

### (c) Por qué ese diseño

- Se respeta la asignación propuesta en `docs/Idea.md` §4 sin conflictos entre
  sí: LEDs (2–6), botones (7–11), buzzer (16) e I2C (14/15) — `docs/DECISIONES.md:29-44`.
- `GPIO14/15` pertenecen a **I2C1** (no I2C0) en la RP2040, por eso se instancia
  `i2c1` — `docs/DECISIONES.md:42-44`.
- **Batería en GPIO26 (ADC0), no en GPIO29 (ADC3)**: en la Pico W, GPIO29 está
  ocupado por el SPI del Wi-Fi (`WL_CLK`) y por el divisor interno VSYS/3 —
  `docs/DECISIONES.md:161-164` y `docs/ESQUEMATICO.md:168-169`.
- Los LEDs usan resistencia limitadora de **330 Ω** (~4 mA, margen sobre el máx.
  recomendado de 12 mA por GPIO): `docs/ESQUEMATICO.md:62-72`.

### (d) Bugs/trampas

- **Trampa 0x3C vs 0x78**: la misma dirección en notación 7-bit (`0x3C`) y 8-bit
  (`0x78`). El SDK RP2040 (`i2c_write_blocking`) usa 7 bits. Documentado en
  `docs/ESQUEMATICO.md` §3.3 y en `CHANGELOG.md:79-80` (v0.9.5).
- **GPIO29 no es libre** en la Pico W: usar VSYS/3 para medir la batería era un
  error (conflicto con el Wi-Fi); se migró a GPIO26 — `CHANGELOG.md:504-508` (v0.2.1).

### (e) Preguntas de defensa

1. **¿Por qué la batería no se mide por GPIO29/ADC3 (VSYS)?**
   En la Pico W, GPIO29 está tomado por el SPI del CYW43 (`WL_CLK`) y por el divisor
   interno VSYS/3; GPIO26 (ADC0) está libre y evita el conflicto y el lock del CYW43.
2. **¿Por qué el OLED va en I2C1 y no I2C0?**
   GPIO14/15 son los pines de I2C1 en la RP2040; además I2C0 (GPIO0/1) queda libre
   para la UART de depuración.
3. **¿Qué significa "0x3C = 0x78"?**
   Es la misma dirección: 0x3C en 7 bits, 0x78 en 8 bits (7-bit << 1). El SDK usa
   la forma de 7 bits.
4. **¿Por qué concentrar todos los pines en un solo header?**
   Única fuente de verdad: cambiar un pin solo toca `pin_defs.hpp`, y es fácil de
   verificar contra el esquema (`docs/ESQUEMATICO.md`).

---

## 2. Display OLED (SSD1306 / SH1106, I2C1)

### (a) Qué hace

Driver de display OLED monocromo **128×64** por I2C1, con framebuffer de 1024 bytes
y texto 5×7. Soporta dos controladores (SSD1306 0.96" y SH1106 1.3") seleccionables
**en runtime** desde `/display` y persistidos en Flash.

### (b) Cómo funciona (código)

**Protocolo I2C.** Toda transacción empieza con un "byte de control": `0x00` =
comandos, `0x40` = datos (GDDRAM/framebuffer) — `src/display_driver.cpp:5-8`.
`send_commands()` escribe `0x00` sin STOP y luego los comandos con STOP —
`src/display_driver.cpp:62-68`. La dirección se auto-detecta probando `0x3C` y
luego `0x3D` con un write de 1 byte y chequeando ACK — `src/display_driver.cpp:44-56`.

**Inicialización.** `init(Controller)` configura I2C1 **una sola vez** (flag
estático `i2c_configured`, para poder re-inicializar en caliente al cambiar de chip)
— `src/display_driver.cpp:79-87`. Luego elige una de dos secuencias:

- `kInitSSD1306[]` — `src/display_driver.cpp:100-118`: charge-pump `0x8D,0x14` (línea 106),
  DC-DC `0xAD,0x8B` "compat" (107), addressing horizontal `0x20,0x00` (108),
  COM `0xDA,0x12` (111), pre-carga `0xD9,0x22` (113), VCOM `0xDB,0x35` (114).
- `kInitSH1106[]` — `src/display_driver.cpp:119-138`: DC-DC `0xAD,0x8B`
  (125), tensión de bomba `0x31` = 7.4 V (126), charge-pump `0x8D,0x14` "compat" (127),
  addressing **por página** `0x20,0x02` (128), COM `0xDA,0x12` (131), contraste máx.
  `0x81,0xFF` (132).

La selección es `seq = (g_controller == SH1106) ? kInitSH1106 : kInitSSD1306` —
`src/display_driver.cpp:140-141`.

**Volcado del framebuffer (`show()`).** El SH1106 (RAM 132×64) **no** soporta las
órdenes de rango 0x21/0x22, así que se vuelca **página por página** con offset de
columna +2: `0xB0|page`, columna baja `0x02`, columna alta `0x10` —
`src/display_driver.cpp:184-202`. El SSD1306 usa addressing horizontal `0x21/0x22`
y un solo volcado — `src/display_driver.cpp:205-214`.

**Selección en runtime persistida.** `GameConfig::display_controller` (uint8_t,
default = macro `PICOREFLEX_DISPLAY_CONTROLLER`) — `include/config.hpp:156-158`. El
handler `/display` lo lee con `?controller=0|1`, y `game_logic.cpp` lo sincroniza
Flash↔RAM — `src/game_logic.cpp:215` (apply) y `src/game_logic.cpp:241` (sync).

**Auto-test.** Al final de `init()` se envía "Display All ON" `0xA5` durante 1 s y
luego `0xA4`, con log serial `rc=… (ACK/NACK)` — `src/display_driver.cpp:217-235`.

### (c) Por qué ese diseño

- I2C a 400 kHz con framebuffer en RAM simplifica el dibujo (se vuelca todo con
  `show()`) y el SSD1306 muestra más info que un LCD1602 — `docs/DECISIONES.md:191-198`.
- **Por qué el "cross-charge-pump" (doble charge-pump):** la auto-detección por ACK
  en `0x3C` **no distingue** SSD1306 de SH1106 (ambos responden igual). Si el init
  mandara una sola fuente de tensión según el controlador elegido y el panel físico
  fuera el *otro* chip, su comando de bomba se ignoraría → negro total. Por eso
  **las dos secuencias mandan AMBAS fuentes** (`0x8D,0x14` + `0xAD,0x8B`), y cada
  una es ignorada con seguridad por el chip que no la usa — `docs/DECISIONES.md:540-554`.

### (d) Bugs/trampas y su fix

- **Pantalla en negro (mismatch de charge-pump):** init con una sola fuente de
  tensión por chip → si el panel real era el otro controlador, su bomba se ignoraba
  → píxeles sin tensión interna → negro. Fix: doble charge-pump + pre-carga `0xD9,0x22`
  + VCOM `0xDB,0x35` + `0x31` en SH1106 — `CHANGELOG.md:45-48` y `docs/DECISIONES.md:551-554`.
- **SH1106 en blanco (addressing):** el direccionamiento horizontal dejaba el SH1106
  en blanco; se cambió a addressing por página (`0xB0`+columna) y COM `0x12` —
  `CHANGELOG.md:34-35` y `docs/DECISIONES.md:555-557`.
- **Auto-test `0xA5` como diagnóstico:** enciende TODOS los píxeles sin leer RAM, por
  lo que un destello blanco demuestra que panel + charge-pump + alimentación funcionan;
  `ACK` + sin destello = fallo físico del módulo, no de software — `CHANGELOG.md:55-57`
  y `docs/DECISIONES.md:558-564`.

### (e) Preguntas de defensa

1. **¿Qué diferencia hay entre `0x8D,0x14` y `0xAD,0x8B`?**
   `0x8D,0x14` enciende el **charge-pump** del SSD1306; `0xAD,0x8B` enciende el
   **DC-DC interno** del SH1106. Son mecanismos distintos de generar la tensión
   interna del panel.
2. **¿Por qué se mandan los dos comandos en las dos secuencias?**
   Porque el ACK de I2C no distingue el chip; mandando ambos, el controlador
   presente usa el suyo y el otro es un no-op → init robusto ante selección errónea.
3. **¿Por qué el SH1106 usa addressing por página y offset +2?**
   El SH1106 tiene RAM de 132 columnas y no soporta 0x21/0x22; el área visible de
   128 px empieza en la columna 2, por eso se suma +2 al volcar cada página.
4. **¿Qué prueba el self-test 0xA5?**
   Que panel + charge-pump + alimentación funcionan, independiente del
   direccionamiento/offset/contraste (0xA5 no lee RAM). ACK sin destello = hardware muerto.
5. **¿Por qué el cambio de controlador no exige reiniciar?**
   `init()` puede re-llamarse: I2C1 ya está configurado (flag estático) y solo se
   reenvía la secuencia del chip elegido.

---

## 3. Debounce de pulsadores por PIO

### (a) Qué hace

Cada pulsador tiene su **propia state machine (SM) PIO** que muestrea el pin,
detecta el flanco de bajada y emite el evento **ya filtrado** (sin rebote). La CPU
ya **no filtra**: la IRQ solo captura el timestamp, liberando CPU de la IRQ GPIO.

### (b) Cómo funciona (código)

**Programa PIO** — `button_debounce.pio:27-62` (26 instrucciones, una SM por botón):

1. `pull` → lee del TX FIFO el **índice del botón** (inyectado por la CPU) a OSR.
2. `wait 1 pin 0` → se arma cuando el botón está **suelto** (pin alto, pull-up).
3. `wait 0 pin 0` → detecta el **primer flanco de bajada** (la pulsación).
4. `mov isr, osr` + `push noblock` + `irq set 0` → envía el índice al RX FIFO y
   **notifica a la CPU de inmediato** (el timestamp lo captura la IRQ).
5. **5 bloques de retardo** (`set x,31` / `set y,31` / `jmp y--` / `jmp x--`) →
   lockout de ~5.12 ms que suprime el rebote posterior.
6. `.wrap` → vuelve a `wait 1 pin 0` (esperar soltar antes de re-armarse).

**Configuración en CPU** — `src/game_logic.cpp:273-310`:

- Reloj de muestreo 1 MHz: `sm_div = clock_get_hz(clk_sys) / BUTTON_DEBOUNCE_SAMPLE_HZ`
  — `src/game_logic.cpp:273` (macro en `include/pin_defs.hpp:50`).
- Programa cargado **una vez por bloque** PIO (ocupa 26 de 32 instrucciones):
  `pio_add_program(pio0/pio1, …)` — `src/game_logic.cpp:277-278`.
- Mapeo: botones 0..3 → **pio1 SM0..SM3**; botón 4 → **pio0 SM1** (pio0 SM0 queda
  libre para WS2812) — `src/game_logic.cpp:288-289`.
- Por botón: `gpio_init` + pull-up + `sm_config_set_in_pins(pin)` +
  `sm_config_set_clkdiv(sm_div)` + `pio_sm_put(pio, sm, i)` (índice por TX FIFO) +
  `pio_sm_set_enabled` — `src/game_logic.cpp:280-301`.
- IRQ por bloque: `irq_set_exclusive_handler(PIO0_IRQ_0/PIO1_IRQ_0, …)` —
  `src/game_logic.cpp:305-310`.

**Handler de IRQ** — `src/game_logic.cpp:167-185`: captura `time_us_32()` **una vez**
lo más cerca del flanco, limpia el flag y drena el RX FIFO guardando índice +
timestamp (`g_button_index`, `g_button_ts_us`, `g_button_pending`).

### (c) Por qué ese diseño

- **Por qué PIO y no software/IRQ:** el PIO es un co-procesador de I/O determinista
  que muestrea y filtra **en hardware**, sin consumir CPU ni latencia de IRQ; la
  CPU solo recibe el flanco limpio. El comentario en `src/game_logic.cpp:155-158`
  lo resume: "la CPU NO filtra: la IRQ solo captura `time_us_32()` … ese rebote lo
  absorbe ahora el PIO". El timing de reacción en µs se conserva porque el timestamp
  se mide en el **primer** contacto (`wait 0 pin 0` + `irq set 0`), no en el flanco
  ya asentado — `button_debounce.pio:8-12`.
- Una SM por botón (no una SM para los 5) porque la RP2040 no tiene operación
  AND/XOR de pines en PIO; además un lockout global rompería el disparo simultáneo
  de dos botones.
- 1 MHz = resolución de 1 µs, idéntica a `time_us_32()` — `button_debounce.pio:21-23`
  y `include/pin_defs.hpp:49-50`.

### (d) Bugs/trampas

- **Antes:** el debounce era software en la IRQ GPIO (`BUTTON_DEBOUNCE_US` +
  chequeo de `g_last_edge_us` en `game_logic.cpp`), consumiendo CPU en cada IRQ de
  rebote. Se reemplazó por PIO — `CHANGELOG.md:221-222` (v0.7.0, "Debounce de
  pulsadores por PIO").
- **Trampa:** `wait 0 pin 0` no es "flanco" sino "nivel"; para volver a armarse el
  botón debe **soltarse** primero (`.wrap` a `wait 1 pin 0`), evitando retrigger
  por mantener presionado.

### (e) Preguntas de defensa

1. **¿Por qué PIO en lugar de un debounce por software en la IRQ?**
   Libera CPU y latencia: el filtrado es determinista en hardware; la IRQ solo lee
   `time_us_32()` y guarda índice/timestamp.
2. **¿Cómo se mantiene el timing de reacción en µs?**
   El timestamp se toma en el **primer** flanco de bajada (`wait 0 pin 0` → `irq set 0`),
   no en el flanco ya asentado; 1 MHz da resolución de 1 µs.
3. **¿Por qué una SM por botón y no una para los 5?**
   La PIO de la RP2040 no tiene AND/XOR de pines para multiplexar; y un lockout
   global impediría detectar dos botones simultáneos.
4. **¿Cómo sabe la CPU qué botón se pulsó?**
   El índice se inyecta por TX FIFO (`pio_sm_put`) y se devuelve por RX FIFO con
   `push`; la IRQ lo lee con `pio_sm_get`.
5. **¿Cuánto dura el lockout y por qué?**
   ~5.12 ms (5 × 1024 ciclos a 1 MHz), el equivalente al antiguo `BUTTON_DEBOUNCE_US`
   de 5000 µs, para suprimir el rebote mecánico.

---

## 4. Buzzer pasivo (PWM, GPIO16)

### (a) Qué hace

Genera feedback sonoro (acierto, fallo, timeout, cuenta regresiva, game over) con
un **buzzer pasivo** controlado por un slice PWM de la RP2040.

### (b) Cómo funciona (código)

- **Inicialización** — `src/audio.cpp:53-74`: `gpio_set_function(BUZZER_PIN,
  GPIO_FUNC_PWM)`, obtiene slice/canal con `pwm_gpio_to_slice_num/chan`, fija el
  divisor `AUDIO_CLKDIV = 100.0f` (`src/audio.cpp:41`) y deja el canal en 0.
- **Generación del tono** — `src/audio.cpp:79-100`: la frecuencia se obtiene del
  periodo del PWM, `f = clk_sys / (clkdiv · (wrap+1))`, de donde
  `wrap = g_base_hz / freq - 1` (`src/audio.cpp:89`). El duty se fija al 50 % con
  `pwm_set_chan_level(slice, chan, wrap/2)` (`src/audio.cpp:93`). Al terminar
  `vTaskDelay(duration)`, el canal vuelve a 0 (silencio).
- **Efectos predefinidos** — `src/audio.cpp:105-131`: acierto 880 Hz (La5) → 1175 Hz
  (Re6); fallo 220 Hz (La3); timeout 440 Hz; game over melodía descendente
  Do5→Do4.

### (c) Por qué ese diseño

- Un buzzer **pasivo** solo suena mientras recibe una onda cuadrada; el PWM genera
  esa onda con la frecuencia = tono y el duty = "volumen" — `include/audio.hpp:20-22`.
- Divisor fijo (`AUDIO_CLKDIV=100`) y `wrap` derivado de la frecuencia: cubre el
  rango audible (20 Hz..8 kHz) sin desbordar el registro `wrap` de 16 bits —
  `src/audio.cpp:12-14` y `docs/DECISIONES.md:207-214`.
- El GPIO no entrega corriente suficiente para el buzzer; se usa un **driver a
  transistor** (2N2222/BC547 con R de base 1 kΩ) — `docs/ESQUEMATICO.md:101-112`.

### (d) Bugs/trampas

- **Trampa:** `f = base/(wrap+1)` — el `-1` en `wrap = base/f - 1` viene de que el
  contador cuenta 0..wrap; olvidarlo desafina el tono — `src/audio.cpp:88-89`.
- **Rango acotado:** `play_tone` recorta a 20..8000 Hz para evitar `wrap=0` o
  desbordamiento de 16 bits — `src/audio.cpp:84-86`.

### (e) Preguntas de defensa

1. **¿Por qué el buzzer necesita PWM y no un simple GPIO?**
   Es pasivo: requiere una señal alterna (onda cuadrada) para vibrar; el PWM la
   genera con frecuencia ajustable.
2. **¿Cómo se define la frecuencia de un tono?**
   Por el periodo del PWM: `f = clk_sys / (clkdiv · (wrap+1))`; se calcula `wrap`
   a partir de la frecuencia pedida.
3. **¿Qué representa el nivel del canal PWM?**
   El duty cycle (50 %): define la "fuerza"/volumen de la vibración.
4. **¿Por qué un transistor y no conectar el buzzer directo al GPIO?**
   El GPIO no entrega la corriente que consume el buzzer; el transistor lo conmuta
   desde la alimentación.

---

## 5. Batería (ADC + divisor resistivo 10k/22k)

### (a) Qué hace

Mide la tensión de la batería LiPo 1S con el ADC de 12 bits, la convierte a mV,
estima el porcentaje con una curva de descarga calibrable y dispara una alerta
bajo el 15 %.

### (b) Cómo funciona (código)

- **Inicialización** — `src/battery_monitor.cpp:36-40`: `adc_init()` +
  `adc_gpio_init(VSYS_PIN)` (GPIO26/ADC0).
- **Lectura** — `src/battery_monitor.cpp:45-67`: promedia **16 muestras**
  (`BATTERY_SAMPLES`, `src/battery_monitor.cpp:26`) para reducir ruido; convierte
  `vpin_mv = avg * 3300 / 4095` (`src/battery_monitor.cpp:56`) y reconstruye la
  batería con el divisor: `vbat_mv = vpin_mv * (R1+R2)/R2 = vpin_mv * 32/22`
  (`src/battery_monitor.cpp:59`). Suma la compensación por carga `load_comp_mv`
  (acotada a 500 mV) — `src/battery_monitor.cpp:64-66`.
- **Conversión a porcentaje** — `src/battery_monitor.cpp:72-95`: curva por tramos
  con `v_full_mv`=100 %, `v_mid_mv`=50 %, `v_empty_mv`=0 % e interpolación lineal;
  guarda de integridad `vf > vm > ve` (cae a 4200/3700/3000 si está corrupta) —
  `src/battery_monitor.cpp:84`.
- **Alerta** — `is_low_battery(percent) = percent < 15` — `src/battery_monitor.cpp:97-99`.
- **Calibración** — `BatteryCalibration { v_full_mv=4200, v_mid_mv=3700,
  v_empty_mv=3000, load_comp_mv=0 }` — `include/config.hpp:112-116`; se reemplaza
  en runtime con `set_calibration()` — `src/battery_monitor.cpp:101-106`.

### (c) Por qué ese diseño

- **Divisor externo 10k/22k** baja la tensión de la batería al rango 0..3.3 V del
  ADC: con 4.2 V el pin recibe 2.89 V (sin recorte) — `docs/DECISIONES.md:156-171`
  y `docs/ESQUEMATICO.md:114-132`.
- **GPIO26/ADC0, no GPIO29/ADC3 (VSYS/3):** GPIO29 está ocupado por el Wi-Fi; el
  divisor externo en GPIO26 evita el conflicto y el lock del CYW43 —
  `docs/DECISIONES.md:161-164` y `include/pin_defs.hpp:85-86`.
- **Curva calibrable:** la curva LiPo no es lineal; los 3 puntos permiten ajustar
  la estimación sin recompilar — `src/battery_monitor.cpp:72-95`.

### (d) Bugs/trampas

- **VSYS/3 → divisor externo (v0.2.1):** medir por el divisor interno VSYS/3 era
  incorrecto en la Pico W (GPIO29 en conflicto con el Wi-Fi); se migró a GPIO26 con
  divisor 10k/22k y `Vbat = Vpin · 32/22` — `CHANGELOG.md:504-508`.
- **Trampa división por cero / calibración corrupta:** si la calibración persistida
  no cumple `ve < vm < vf`, `voltage_to_percent` cae a los valores por defecto para
  no dividir por cero — `src/battery_monitor.cpp:82-84`.
- **Compensación por carga acotada:** `load_comp_mv` se limita a 500 mV para evitar
  sobre-corregir la lectura — `src/battery_monitor.cpp:64-65` y `:105`.

### (e) Preguntas de defensa

1. **¿Para qué sirve el divisor resistivo?**
   Para bajar la tensión de la batería (hasta 4.2 V) al rango 0..3.3 V que admite
   el ADC; se reconstruye con `Vbat = Vpin · 32/22`.
2. **¿Por qué 10k/22k y no otros valores?**
   Dan una atenuación 22/32 ≈ 0.6875, de modo que 4.2 V → 2.89 V en el pin (< 3.3 V,
   sin recorte) y la corriente de fuga por el divisor es baja.
3. **¿Cómo se convierte tensión a porcentaje?**
   Curva por tramos: 4200 mV=100 %, 3700 mV=50 %, 3000 mV=0 %, con interpolación
   lineal entre esos puntos.
4. **¿Por qué se promedian 16 muestras del ADC?**
   Para reducir el ruido de la lectura (el promedio atenúa fluctuaciones puntuales).
5. **¿Qué pasa si la calibración guardada es inválida?**
   La guarda `vf > vm > ve` detecta corrupción y cae a la curva por defecto.

---

## 6. Alimentación (advertencia 3V3, nunca VBUS)

### (a) Qué hace

Define cómo se alimenta el sistema y, críticamente, **advierte que el OLED debe
alimentarse a 3V3 y nunca desde VBUS (5 V)**.

### (b) Cómo funciona (código / docs)

- La advertencia está documentada en el esquemático: *"Alimentación: solo 3V3,
  nunca VBUS (5 V). Estos módulos OLED 1.3" son de 3.3 V…"* —
  `docs/ESQUEMATICO.md:93-99`.
- El aviso se repite en la decisión del display: *"IMPORTANTE: estos OLED 1.3" son
  de 3.3 V; alimentarlos desde VBUS (5 V) puede quemarlos y, además, sube sus
  pull-ups de I2C a 5 V sobre los GPIO14/15 (que no son tolerantes a 5 V)"* —
  `docs/DECISIONES.md:566-569`.

### (c) Por qué ese diseño

- **Batería:** LiPo/Li-Ion **1 celda (1S)**, 3.0–4.2 V (nominal 3.7 V), 1200–2000 mAh;
  carga con **TP4056** y conversor **Step-Up a 5 V** para VSYS — `docs/ESQUEMATICO.md:136-141`.
- **Por qué 3V3 y no VBUS:** la RP2040 no es tolerante a 5 V en sus GPIO; un módulo
  OLED a 5 V eleva sus pull-ups I2C a 5 V sobre SDA/SCL (GPIO14/15), con riesgo de
  dañar la RP2040, además de quemar el charge-pump/DC-DC del propio panel.

### (d) Bugs/trampas

- **Trampa de alimentación del OLED:** si el panel no enciende aun con init correcto
  y el self-test `0xA5` devuelve `ACK` sin destello blanco, el módulo está
  defectuoso (flex agrietado / bomba muerta), no reparable por software —
  `docs/ESQUEMATICO.md:96-99` y `docs/DECISIONES.md:563-565`.

### (e) Preguntas de defensa

1. **¿Por qué el OLED va a 3V3 y no a 5 V (VBUS)?**
   Es un módulo de 3.3 V: a 5 V se puede quemar su charge-pump/DC-DC y, peor, sube
   los pull-ups I2C a 5 V sobre GPIO14/15 que no son tolerantes a 5 V.
2. **¿Cómo se alimenta la Pico W desde una LiPo 1S?**
   La batería (3.0–4.2 V) entra directo a VSYS (admite 1.8–5.5 V) o se eleva a 5 V
   con un step-up; se carga con un TP4056.
3. **¿Qué rango de tensión admite VSYS?**
   1.8–5.5 V, por lo que una LiPo 1S puede ir directa sin regulador.
4. **¿Cómo se diagnostica un OLED que no enciende?**
   Con el self-test `0xA5`: si hay `ACK` pero no destello blanco, el fallo es de
   hardware (panel/charge-pump/alimentación), no del direccionamiento ni la RAM.
