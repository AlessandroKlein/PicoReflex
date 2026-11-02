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

Esta sección se divide en dos partes: **2.A** (la lógica de juego: máquina de
estados y matemática del timing) y **2.B** (el algoritmo matemático del PRNG
xorshift32 y el "seed", que es lo más importante del firmware). Ambas son
exhaustivas y con referencias `archivo:línea` reales.

---

### 2.A Lógica de juego (máquina de estados + matemática de timing)

#### (a) Qué hace
Motor de reacción en el **Core 0** que ejecuta una máquina de estados cíclica
(IDLE → COUNTDOWN → RUNNING → EVALUACIÓN → IDLE) y una rama especial para
**Simón Dice**. Configura 5 LEDs (salidas GPIO2–6) y 5 pulsadores (entradas
GPIO7–11 con debounce por PIO), mide la reacción en **microsegundos** con el
temporizador hardware, y actualiza estadísticas/persistencia.

#### (b) Cómo funciona — diagrama de estados

El bucle principal es `game::run()` (`src/game_logic.cpp:536-700`), un `while(true)`
que **nunca retorna** (bloquea el Core 0). Los estados lógicos y sus transiciones:

```
                        (fin de ronda: end_round :418-424
                         o bucle de vuelta al IDLE)
        ┌─────────────────────────────────────────────────┐
        ▼                                                 │
     ┌──────┐   cualquier botón      ┌───────────┐        │
     │ IDLE │───────────────────────▶│ COUNTDOWN │        │
     └──────┘   (:571-575)           └───────────┘        │
        ▲  g_round_active=false      start_with_countdown  │
        │                            (:386-397)           │
        │                          ┌─► start_round (:402-416)
        │                          │   g_round_active=true
        │                          │
        │                          ▼
        │                     ┌─────────┐  hit/miss/timeout
        │                     │ RUNNING │  (reacción :607-668)
        │                     └─────────┘
        │                          │
        │                          ▼
        │                     ┌────────────┐  stats_record + feedback
        │                     │ EVALUACIÓN │  (:675-696)
        │                     └────────────┘
        │                          │
        └──────────────────────────┘  SUDDEN_DEATH→end_round (:693-695)
                                      resto→bucle (persist_if_dirty :699)
```

**Transiciones con línea exacta** (`src/game_logic.cpp`):

| Transición | Línea | Qué dispara |
|---|---|---|
| IDLE → COUNTDOWN | `:570-575` | `!g_round_active` y `g_button_pending` → cualquier botón |
| COUNTDOWN → RUNNING | `:386-397` | `start_with_countdown()` cuenta `start_delay_s` s y llama `start_round()` |
| RUNNING → EVALUACIÓN | `:632-644` | pulsación (`hit`/`miss`/`timeout`) o timeout de tolerancia (`:648-651`) |
| EVALUACIÓN → IDLE | `:693-695` | en `SUDDEN_DEATH` si `!hit` → `end_round()` (`:418-424`); en los demás modos el bucle vuelve arriba |
| IDLE (drenado de eventos web) | `:541-565` | `START_ROUND` → `start_with_countdown`; `STOP_ROUND` → `end_round`; `CONFIG_CHANGED` → persistir |

**Rama SIMÓN DICE** (`cfg.mode == SIMON_SAYS`, `:602-605` → `simon_says_round`):

```
start_round (:409-411): g_seq_length=1; g_sequence[0]=next_range(LED_COUNT)
        │
        ▼
simon_says_round (:501-531)
        │
        ├─► simon_show_sequence (:465-474, llamado en :503): muestra TODA la secuencia
        │        (enciende cada LED + tono, velocidad acelera con la longitud)
        │
        ├─► bucle de validación elemento a elemento (:506-509):
        │        for i in 0..g_seq_length-1: ok = simon_wait_input(cfg, g_sequence[i])
        │        simon_wait_input (:477-498): espera UNA pulsación y compara con expected
        │
        ├─► SI falla (botón equivocado o timeout, :511-519):
        │        play_game_over + effect_lose + stats_record(miss)
        │        g_seq_length=1; g_sequence[0]=next_range(5)  → end_round() → IDLE
        │
        └─► SI acierta TODO (:522-528):
                 effect_win + stats_record(hit)
                 si g_seq_length < SIMON_MAX_SEQ(64): agrega un elemento aleatorio y crece
                 pausa 600 ms → vuelve a mostrar la secuencia (más larga)
```

#### (b) Cómo funciona — matemática del timing

Todo el timing usa el contador **hardware** `time_us_32()` (resolución 1 µs,
un contador de 32 bits que da la vuelta cada 2³² µs ≈ **71,58 min**). Referencias
a `src/game_logic.cpp`:

1. **Intervalo entre estímulos (uniforme en `[min, max]`)** (`:610-611`):
   ```c
   uint32_t span     = cfg.stimulus_max_ms - cfg.stimulus_min_ms + 1u; // :610
   uint32_t delay_ms = cfg.stimulus_min_ms + g_rng.next_range(span);   // :611
   ```
   `next_range(span)` devuelve un valor uniforme en `[0, span-1]`; sumarle
   `stimulus_min_ms` da un valor uniforme en `[stimulus_min_ms, stimulus_max_ms]`
   (ambos extremos incluidos). El `+1u` es el clásico off-by-one de los rangos
   `[a, b]` (hay `b−a+1` valores).

2. **`t0` se captura al encender el LED** (`:620`):
   ```c
   led_on(target);          // :619 — enciende el LED objetivo
   uint32_t t0 = time_us_32();  // :620 — instante de referencia
   ```

3. **`reaction_us = g_button_ts_us - t0`** (`:635`): **resta SIN signo**. Como ambos
   operandos son `uint32_t`, la resta se evalúa **módulo 2³²** (aritmética modular).
   Si el contador hizo *wrap* entre `t0` y `g_button_ts_us` (el contador pasó de
   `0xFFFFFFFF` a `0`), el resultado numérico "parece negativo" pero **el valor sin
   signo es exactamente el tiempo transcurrido módulo 2³²**. Como una reacción humana
   es de decenas/cientos de ms (≪ 71,58 min), el resultado módulo 2³² **coincide con
   el tiempo real** y **no hace falta ningún código especial** para detectar el wrap.
   Es la misma propiedad `(a − b) mod 2³²` que usa cualquier `uint32_t` como
   timestamp monótono.

4. **Tolerancia (ventana para considerar el acierto)** (`:623-626`):
   ```c
   uint32_t tolerance_us =
       (cfg.mode == GameMode::FREE_PLAY)
           ? UINT32_MAX                                  // sin timeout ("infinito")
           : difficulty_tolerance_ms(cfg.difficulty) * 1000u;
   ```
   `difficulty_tolerance_ms()` devuelve 1000/500/250 ms según la dificultad
   (`include/config.hpp:288-295`). En FREE_PLAY la tolerancia es `UINT32_MAX`
   (~71,58 min), es decir, **sin límite de tiempo** (el jugador puede tardar lo que
   quiera).

5. **Fin de ronda por tiempo en modo TIMED** (`:654-655`):
   ```c
   if (cfg.mode == GameMode::TIMED &&
       (time_us_32() - g_round_start_us) > cfg.round_seconds * 1000000u) {
       end_round();
   ```
   `g_round_start_us` se fija en `start_round()` (`:415`); `round_seconds * 1e6`
   convierte segundos a microsegundos. La resta sin signo vuelve a absorber el wrap.

6. **El timestamp se captura DENTRO de la IRQ del PIO**, no en el bucle
   (`src/game_logic.cpp:167-185`): el handler `pio_button_irq()` hace
   `uint32_t now = time_us_32();` (`:169`) **lo más cerca posible del flanco** y lo
   guarda en `g_button_ts_us = now` (`:181`). Así la latencia entre el flanco físico
   y la captura del timestamp es mínima (solo la latencia de la propia IRQ), y el
   bucle de `run()` lee después `g_button_ts_us` sin añadir jitter de polling.

#### (c) Por qué se eligió ese diseño
- **Timestamp en la IRQ**: minimiza latencia/jitter de la medición
  (`docs/DECISIONES.md` §4). Si se capturara en el bucle, el tiempo dependería de la
  fase del `vTaskDelay(5 ms)` y de cuánto tardara el scheduler en despertar.
- **Resta sin signo en vez de comparar `>=`**: elimina una rama de código y es
  correcta por definición del C (`uint32_t` sin signo → aritmética módulo 2³²).
- **Debounce por PIO**: cada botón tiene una state machine que filtra el rebote en
  hardware; la CPU solo captura el timestamp (`src/game_logic.cpp:153-158`).

#### (d) Bugs/trampas
- **Wrap de `span = smax - smin + 1`** si `smin > smax`: el rango se desbordaba.
  Se acotó en `web_server.cpp` (v0.1.3, `CHANGELOG.md:542-543`).
- **Disparador de inicio**: pasó de "dos botones simultáneos" (complejo y propenso a
  fallo) a **cualquier botón** (`docs/DECISIONES.md` §35, `src/game_logic.cpp:570-575`).
- **Pulsaciones "tempranas"** durante el intervalo oscuro: se descartan con
  `g_button_pending = false` (`:615`) justo antes de encender el LED, para que una
  pulsación accidental durante la espera no cuente como reacción instantánea.

#### (e) Preguntas probables de defensa
1. **¿Por qué la resta sin signo absorbe el wrap del contador?** Porque `uint32_t`
   implementa aritmética módulo 2³²; `(g_button_ts_us − t0)` siempre da el tiempo
   transcurrido módulo 2³², que es el tiempo real mientras la reacción sea < 71,58 min.
2. **¿Qué pasa si el contador hace wrap justo en medio de una reacción?** Nada malo:
   el resultado módulo 2³² sigue siendo correcto (p. ej. `t0=0xFFFF_FFF0`,
   `ts=0x0000_0010` → `ts−t0 = 0x20 = 32 µs`, exacto).
3. **¿Por qué capturar el timestamp en la IRQ y no en el bucle?** Para minimizar la
   latencia entre el flanco y la lectura del contador; en el bucle habría jitter de
   hasta un `vTaskDelay(5 ms)`.
4. **¿Cómo se descartan las pulsaciones prematuras?** `g_button_pending = false`
   (`:615`) justo antes de `led_on()`, así solo cuenta la pulsación posterior al
   estímulo.

---

### 2.B Algoritmo matemático del PRNG (xorshift32, el "seed")

#### (a) Qué hace
Genera la secuencia pseudoaleatoria **determinista** que decide el orden de los
LEDs en el modo reacción y la secuencia completa de **Simón Dice**. Es la pieza
central del juego: **una misma semilla produce exactamente la misma secuencia**,
lo que permite partidas reproducibles, récords comparables y pruebas.

#### (b) Cómo funciona — la matemática

1. **Definición exacta (Marsaglia 2003)** — `include/game_logic.hpp:48-55`:
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
   Tres operaciones de *xor-shift* con la terna **(a, b, c) = (13, 17, 5)**.

2. **Linealidad sobre GF(2)**: cada bit del estado es un elemento de GF(2) (el campo
   de dos elementos {0,1} con XOR como suma y AND como producto). XOR es suma módulo 2,
   y cada *shift* (`<<k`, `>>k`) es un mapa **lineal** (una matriz de 32×32 que
   desplaza los bits). Por tanto `x ^= x << k` es la aplicación del operador
   `I + Lᵏ` (donde `L` es la matriz de *shift* izquierda y `I` la identidad). La
   transición completa es un único mapa lineal:
   ```
   x' = T·x,  con  T = (I + L⁵)(I + R¹⁷)(I + L¹³)
   ```
   donde `L`/`R` son las matrices de *shift* izquierda/derecha (el orden del producto
   sigue el orden de ejecución del código: primero `(I+L¹³)`, luego `(I+R¹⁷)`, luego
   `(I+L⁵)`).

3. **Invertibilidad**: cada factor `(I + Lᵏ)` es **invertible** porque `Lᵏ` es
   **nilpotente** (aplicar el *shift* izquierdo 32 veces aniquila cualquier vector:
   `L³² = 0`, luego `(Lᵏ)ᵐ = Lᵏᵐ = 0` para `km ≥ 32`). La inversa de `(I+Lᵏ)` es la
   serie geométrica **finita** `I + Lᵏ + L²ᵏ + … + Lᵏ⁽ᵐ⁻¹⁾` (se anula por telescopaje,
   y en GF(2) los términos intermedios se cancelan de a pares porque `2=0`). Lo mismo
   vale para `(I+Rᵏ)`. Como `T` es producto de matrices invertibles, **`T` es
   invertible** → es una **permutación (biyección)** de los 2³² estados.

4. **Periodo**: con la terna (13, 17, 5) el periodo es **2³² − 1**, el máximo posible
   para un estado de 32 bits no nulo. Todos los estados no nulos forman **un único
   ciclo** de longitud 2³² − 1 = 4 294 967 295 (el estado 0 queda fuera, ver punto 5).

5. **Estado cero = punto fijo** (`0 << k = 0` y `0 ^ 0 = 0`): si el estado fuera 0,
   `next()` devolvería 0 para siempre (el generador queda "atascado"). Por eso la
   semilla **DEBE ser no nula**: `set_seed(0)` sustituye por la constante
   `0x9E3779B9u` (`include/game_logic.hpp:45`):
   ```c
   void set_seed(uint32_t s) { state_ = (s != 0u) ? s : 0x9E3779B9u; }
   ```

6. **`0x9E3779B9` = 2 654 435 769 = ⌊2³²/φ⌋**, con `φ = (1+√5)/2 ≈ 1,6180339887`
   (la **razón áurea**): 2³²/φ ≈ 2 654 435 769,497 → la parte entera es `0x9E3779B9`.
   Es una constante "nothing-up-my-sleeve" (derivada de una constante matemática, no
   elegida arbitrariamente) con **buen mezclado de bits**, estándar en *seeding* de
   hashes y PRNG (aparece en el *golden-ratio hash*).

7. **`next_range(n) = next() % n`** (`include/game_logic.hpp:58-61`): el **sesgo del
   módulo**. Si la salida fuera uniforme en `[0, 2³²)`, el residuo `2³² mod n`
   aparecería una vez más. Aquí hay un matiz importante: **xorshift32 nunca emite 0**
   (el estado 0 es un punto fijo excluido del ciclo), así que su salida en un periodo
   completo es exactamente el conjunto `{1, 2, …, 2³²−1}` (2³²−1 valores). Para
   `n = 5` (= `LED_COUNT`): `2³² − 1 = 5 · 858 993 459`, **divisible exacto** → en el
   ciclo completo **no hay sesgo**. Aun en el peor caso (si se considerara el dominio
   `[0, 2³²)`), el sesgo sería de 1 aparición extra sobre 2³², un sesgo relativo
   `≈ 1/2³² ≈ 2,3·10⁻¹⁰`, **despreciable**. Por eso para `n` pequeño **no hace falta
   *rejection sampling*** (rechazar y resortear), que sí sería necesario para rangos
   grandes cercanos a 2³².

8. **Determinismo**: misma semilla → misma secuencia. Como `T` es determinista y
   `set_seed` fija exactamente el estado inicial, dos ejecuciones con la misma semilla
   producen **la misma secuencia de Simón** (mismos LEDs en el mismo orden) y **el
   mismo orden de estímulos** en el modo reacción. Esto es lo que hace reproducibles
   las partidas.

9. **Siembra real** (`src/game_logic.cpp:406`):
   ```c
   g_rng.set_seed(cfg.seed != 0u ? cfg.seed : get_rand_32());
   ```
   Si `cfg.seed != 0` se usa la **semilla fija configurada** (reproducible); si es 0
   se usa **entropía hardware** `get_rand_32()` de `pico_rand` (aleatorio real). Es
   decir: `seed=0` significa "aleatorio", y cualquier otro valor "determinista".

10. **Persistencia de la semilla** (`src/game_logic.cpp:404`):
    ```c
    g_persist.last_seed = cfg.seed;
    ```
    La última semilla usada se guarda en Flash (`last_seed` dentro de `PersistentData`),
    junto al resto de la configuración persistida.

#### (c) Por qué se eligió ese diseño
- **`xorshift32` propio y no `rand()`/`pico_rand`**: `pico_rand` **no expone una
  función para fijar la semilla** (se auto-siembra con entropía), y para el modo
  "semilla determinista" hace falta una secuencia **idéntica en cada reinicio**
  (`include/game_logic.hpp:35-37`, `docs/DECISIONES.md` §3). `xorshift32` es trivial
  (3 líneas), sin estado oculto, y su matemática es **demostrable** (permutación,
  periodo máximo).
- **Terna (13, 17, 5)**: es una de las ternas publicadas por Marsaglia que cumple las
  condiciones de *periodo máximo* (el polinomio característico de `T` es primitivo en
  GF(2)). No es un número mágico arbitrario: fue elegido y verificado para maximizar
  el periodo y el mezclado.

#### (d) Bugs/trampas
- **Semilla 0**: es el error clásico. Un estado todo-cero es punto fijo y produce
  ceros infinitos; por eso `set_seed(0)` lo reemplaza por `0x9E3779B9`
  (`include/game_logic.hpp:45`).
- **Sesgo del módulo**: despreciable para `n=5`, pero si algún día se usara
  `next_range` con un `n` grande (cercano a 2³²) habría que pasar a *rejection
  sampling* (o a un `uint64_t`). El comentario del propio código lo advierte
  (`include/game_logic.hpp:59`).
- **"Reproducible" ≠ "seguro"**: xorshift32 es **predecible** (lineal) y por tanto NO
  sirve para criptografía ni para secretos; solo para jugabilidad. No confundir con un
  CSPRNG.

#### (e) Preguntas probables de defensa
1. **¿Por qué 13, 17 y 5?** Es una terna publicada por Marsaglia (2003) que da
   **periodo máximo 2³²−1**: el polinomio característico de `T` es primitivo en
   GF(2), así que todos los estados no nulos forman un único ciclo.
2. **¿Cuál es el periodo y por qué?** `2³² − 1`. Porque `T` es una permutación
   (invertible) sin más punto fijo que el 0, y el ciclo de los estados no nulos tiene
   longitud máxima.
3. **¿Qué pasa si la semilla es 0?** El generador quedaría atascado en 0 para siempre
   (0 es punto fijo). Por eso `set_seed(0)` usa `0x9E3779B9` = ⌊2³²/φ⌋.
4. **¿Por qué el módulo no introduce sesgo relevante?** Para `n=5`, `next() % 5`
   mapea los 2³²−1 valores no nulos (divisibles por 5) de forma exacta; y aun en el
   peor caso el sesgo sería 1/2³² ≈ 2,3·10⁻¹⁰, despreciable.
5. **¿Cómo se garantiza que dos partidas con la misma semilla sean idénticas?**
   Porque `set_seed(cfg.seed)` fija exactamente el estado inicial y `T` es
   determinista: la misma semilla produce la misma secuencia de `next()` y por tanto
   la misma secuencia de LEDs/Simón.

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
