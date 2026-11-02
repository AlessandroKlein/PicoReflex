# Discurso oral de defensa — PicoReflex

> Guión hablado completo para defender el proyecto frente al tribunal.
> Una sección por filmina, alineada a las 12 de
> `Presentacion_Defensa_PicoReflex.pptx`. No es un resumen: es **lo que se dice en
> voz alta**, en primera persona.
>
> Tono: defensa académica — materia **Microprocesadores · Tecnicatura en
> Desarrollo de Software**. Firmware v0.9.7 · Pico SDK 2.3.0 · FreeRTOS SMP.
>
> **Énfasis (~50 % del tiempo):** filminas 4 a 9 — mecanismo del juego (máquina de
> estados + matemática del timing) y seed (xorshift32). Ahí hay que detenerse y
> explicar en dos niveles: primero con palabras simples, después con la matemática.

---

## 1. Portada

### Guión hablado

Buenos días. Les voy a presentar **PicoReflex**, un juego de reacción sobre una
**Raspberry Pi Pico W**, desarrollado íntegramente en **C/C++ con el SDK oficial
de Raspberry Pi** y con **FreeRTOS** corriendo sobre sus dos núcleos. La propuesta
es sencilla —hay cinco LEDs y cinco pulsadores, y el sistema mide cuánto tardás en
reaccionar—, pero lo interesante está en **cómo** se mide ese tiempo: con
precisión de **microsegundos**, con un generador pseudoaleatorio cuya semilla hace
reproducible cada partida, y con un servidor web embebido que permite configurar y
actualizar el firmware **por aire**.

Voy a seguir este orden: primero qué es y la arquitectura; después el corazón del
trabajo, que es el **mecanismo del juego** y la **matemática de la semilla**;
después Simón Dice, precisión, red y hardware; y cierro con las preguntas que
seguro me van a hacer.

### Qué ampliar si preguntan

- Que el proyecto no usa Arduino ni MicroPython: es **Pico SDK 2.3.0** (C/C++)
  con el toolchain oficial `arm-none-eabi`, `PICO_BOARD=pico_w`.
- Que el objetivo no es solo el juego, sino demostrar dominio de **tiempo real**,
  **concurrencia SMP**, **red embebida** y **matemática aplicada** a un PRNG.
- Que todo el código está versionado y la documentación técnica acompaña cada
  decisión (`docs/estudio/firmware.md`, `red.md`, `hardware.md`).

### Pregunta probable + respuesta

**P:** ¿Por qué una Pico W y no un Arduino o un ESP32?
**R:** Porque el RP2040 de la Pico W me permite trabajar con los **dos núcleos**
y el **PIO** (una máquina de estados por hardware por botón) con el SDK oficial
de C, y la radio CYW43439 me da Wi-Fi para el panel web y la OTA. Es la
combinación que mejor encaja con lo que quería demostrar: tiempo real y
concurrencia real, no un loop con `delay()`.

---

## 2. ¿Qué es PicoReflex?

### Guión hablado

PicoReflex es un **entrenador de reflejos**: tengo cinco LEDs numerados y cinco
pulsadores, y cuando se enciende un LED tenés que pulsar el botón que le
corresponde. El sistema mide tu **tiempo de reacción en microsegundos** —no en
milisegundos— y lo registra con estadísticas y récords.

Tiene **cuatro modos de juego**: libre, contrarreloj, muerte súbita y **Simón
Dice**, que es el que más exprime la lógica de secuencias. Y algo que me parece
clave: la **semilla es determinista**, así que dos personas pueden competir con la
*misma* secuencia de luces y los récords son comparables.

### Qué ampliar si preguntan

- El tiempo se captura dentro de la **IRQ del PIO** con `time_us_32()`, no en el
  bucle principal, para no sumar jitter.
- La configuración (semilla, dificultad, modo, nombre) se cambia **en vivo** desde
  el panel web, sin recompilar.
- Hay actualización de firmware **OTA A/B** con rollback automático si la imagen
  nueva no arranca.

### Pregunta probable + respuesta

**P:** ¿Por qué "en microsegundos" si un humano reacciona en cientos de
milisegundos?
**R:** Porque el límite lo pone la **precisión de la medición**, no la fisiología.
Un reloj de 1 ms te da 3 cifras; uno de 1 µs te da 6, y te permite ver la
**dispersión** real entre reacciones, no un número redondeado. Además demuestra
que sé medir tiempo con un contador de hardware y que entiendo qué pasa cuando
ese contador desborda.

---

## 3. Arquitectura dual-core (FreeRTOS SMP)

> **Filmina con diagrama:** el "Diagrama de bloques". Recorrerlo de izquierda a
> derecha: entrada (botones por PIO) → Core 0 (juego) → estado compartido →
> Core 1 (red/display) → salida (web + OLED). Señalar la caja del medio, que es
> la comunicación entre núcleos.

### Guión hablado

La base es un **RP2040 con dos núcleos** y un chip de Wi-Fi, y arriba corre
**FreeRTOS en modo SMP**. Tengo dos tareas bien separadas: `game_task`, que vive
en el núcleo 0 y hace todo el juego, las interrupciones y la escritura en Flash; y
`network_task`, en el núcleo 1, que atiende el Wi-Fi, el servidor web, el display
y la batería. Las dos refrescan el **watchdog** por si algo se cuelga.

Lo importante del diagrama es la caja del medio: las tareas **no comparten memoria
a lo loco**. Usan una **sección crítica** para el estado y una **cola** para los
eventos —por ejemplo, cuando desde la web toco "iniciar", el evento viaja por la
cola del núcleo 1 al núcleo 0. El arranque es: watchdog, estado compartido,
creación de las dos tareas, y recién ahí arranca el scheduler.

### Qué ampliar si preguntan

- Prioridades y stacks: `game_task` 2048 B con prioridad IDLE+2; `network_task`
  4096 B con IDLE+3.
- `cyw43_arch_init()` se hace **dentro** de `network_task` (patrón oficial de los
  ejemplos FreeRTOS del SDK), no en `main()` antes del scheduler.
- Por qué `critical_section` + `queue_t` en lugar de FIFOs: los FIFOs del SDK son
  de propósito general, mientras que la cola permite encolar hasta 16 eventos sin
  bloquear al productor.

### Pregunta probable + respuesta

**P:** Con FreeRTOS SMP, ¿una tarea está atada a un núcleo fijo?
**R:** No —SMP permite migrar—, pero acá la afinidad se fija por diseño: el juego
y la red quedan en núcleos separados para que una tarea de red que se bloquea no
congele la medición del juego, y al revés. Lo documenté en
`src/main.cpp`, donde se crean las tareas con su afinidad.

---

## 4. Mecanismo de juego — máquina de estados

> **Filmina con diagrama:** el "Flujo del juego (Core 0)". Recorrerlo siguiendo
> las flechas de la máquina de estados: IDLE → COUNTDOWN → RUNNING → EVALUACIÓN →
> de vuelta a IDLE, y señalar el desvío de SIMON_SAYS.

### Guión hablado

Esta es la primera parte del corazón del trabajo. El juego es una **máquina de
estados** cíclica dentro de `game::run()`, que es un bucle que **nunca retorna**
—está en `src/game_logic.cpp:536` en adelante. Los estados son: **IDLE**, esperando
que alguien pulse un botón; **COUNTDOWN**, la cuenta regresiva de arranque;
**RUNNING**, donde se enciende un LED y se mide la reacción; y **EVALUACIÓN**, donde
decido si fue acierto, fallo o timeout, y vuelvo a empezar.

Cada transición tiene su disparador y su línea de código: pasar de IDLE a
COUNTDOWN es cualquier botón (`:570-575`); de COUNTDOWN a RUNNING la hace la cuenta
regresiva (`:386-397`); de RUNNING a EVALUACIÓN, la pulsación o el vencimiento del
tiempo (`:632-644` y `:648-651`); y de EVALUACIÓN a IDLE, en muerte súbita, el fallo
termina la ronda (`:693-695`). Aparte está el desvío de **Simón Dice**, que tiene
su propia lógica de secuencia.

### Qué ampliar si preguntan

- El drenado de eventos web se hace en el mismo bucle (`:541-565`): un evento
  START/STOP/CONFIG encolado desde el núcleo 1 se procesa en el núcleo 0 sin
  interrumpir la máquina de estados.
- La máquina es **no bloqueante**: entre estímulos no usa `sleep_ms`, usa un
  temporizador de hardware y sigue respondiendo a eventos.
- Por qué una máquina de estados explícita y no `if` anidados: cada estado tiene
  una responsabilidad y es fácil probar cada transición por separado.

### Pregunta probable + respuesta

**P:** ¿Qué pasa si llega un evento web justo en medio de una ronda?
**R:** No interrumpe nada: los eventos se encolan y se **drenan al principio del
bucle** (`:541-565`). Si es un "detener", se procesa de forma limpia; si es un
"cambiar configuración", se aplica para la siguiente ronda. Así la medición en
curso nunca se corrompe.

---

## 5. Mecanismo de juego — matemática del timing

### Guión hablado

Acá está la parte que más me gusta: **cómo se mide el tiempo**. Todo gira
alrededor de `time_us_32()`, un contador de hardware que cuenta microsegundos y
que, al ser de 32 bits, **da la vuelta** cada 2³² microsegundos —unos 71 minutos
y medio.

El flujo es: calculo un `delay_ms` **uniforme** entre un mínimo y un máximo
(`:610-611`) para que el jugador no pueda anticipar; capturo `t0` cuando enciendo
el LED (`:620`); y cuando llega la pulsación hago `reaction_us = g_button_ts_us −
t0` (`:635`). Y acá está el detalle fino: esa resta es **sin signo**, así que el
desborde del contador se absorbe solo, **módulo 2³²**, sin ningún código especial
para el wrap. La tolerancia depende de la dificultad (`:623-626`): 1000, 500 o
250 milisegundos, y en modo libre es "sin límite".

### Qué ampliar si preguntan

- El timestamp se captura **dentro de la IRQ del PIO** (`:167-185`), en el primer
  flanco del botón, no cuando el bucle principal se entera —eso minimiza el jitter.
- El modo contrarreloj termina cuando
  `(time_us_32() − g_round_start_us) > round_seconds·1e6` (`:654-655`).
- La resta sin signo funciona porque el intervalo máximo posible entre dos
  eventos es mucho menor que 2³² µs, así que el "módulo 2³²" da exactamente el
  tiempo transcurrido, sin ambigüedad.

### Pregunta probable + respuesta

**P:** ¿Por qué no usás `sleep_ms()` o un timer del sistema para medir?
**R:** Porque `sleep_ms` tiene una granularidad de milisegundos y suma latencia de
scheduling. Con `time_us_32()` leo un contador de hardware de **1 µs** directamente
en la interrupción, así que la medición es del orden del microsegundo y no depende
de cuándo el scheduler me deje correr.

---

## 6. Seed — xorshift32 (el algoritmo)

### Guión hablado

La segunda parte del corazón del trabajo es la **semilla**. Para que las partidas
sean reproducibles —y para que Simón Dice genere secuencias— uso un generador
pseudoaleatorio llamado **xorshift32**, de George Marsaglia, que está en
`src/game_logic.cpp:48-55`. Son exactamente tres líneas:

```c
x ^= x << 13u;
x ^= x >> 17u;
x ^= x << 5u;
```

En palabras simples: tomo un número de 32 bits y lo "revolví" tres veces haciendo
XOR con una versión desplazada de sí mismo. Es extremadamente barato —tres XOR y
tres desplazamientos, nada de multiplicaciones— y, pese a lo simple que parece,
tiene propiedades matemáticas muy fuertes que voy a explicar en la próxima
filmina.

### Qué ampliar si preguntan

- La terna **13, 17, 5** no es arbitraria: es una de las que Marsaglia publicó
  en 2003 y garantiza periodo máximo.
- La idea de "XOR con un shift" es la misma familia que usan otros PRNG (xorshift,
  xoroshiro), pero acá la versión de 32 bits alcanza para el juego.
- Elegí xorshift32 y no `rand()` de C porque es **determinista entre compiladores
  y plataformas**: `rand()` depende de la implementación de la libc.

### Pregunta probable + respuesta

**P:** ¿Por qué no usás `rand()` de la librería estándar?
**R:** Por dos motivos: `rand()` no garantiza la misma secuencia entre
compiladores o versiones de libc, y suele ser más pesado y de peor calidad. Con
xorshift32 yo controlo el algoritmo exacto, es determinista, y tres operaciones
por número lo hacen ideal para un microcontrolador.

---

## 7. Seed — propiedades matemáticas

### Guión hablado

Ahora la matemática, y es lo que le da seriedad al diseño. El XOR es la **suma
módulo 2**, y cada desplazamiento es una **matriz de 32×32** sobre el campo
GF(2). Así que todo el generador es una única transformación lineal:

```
T = (I + L⁵)(I + R¹⁷)(I + L¹³)
```

donde `L` y `R` son los desplazamientos a izquierda y derecha. La clave es que
cada factor es **invertible**: `L` elevado a 32 es la matriz nula —es *nilpotente*—,
y por eso `(I+Lᵏ)` siempre tiene inversa. Como `T` es invertible, es una
**permutación** de los 2³² estados: nada se repite hasta recorrerlos todos.

El resultado es un **periodo de 2³²−1**: todos los estados salvo uno forman un
único ciclo de más de cuatro mil millones. Y el único estado que se queda quieto
es el **cero** —porque `0 ^ 0` es `0`—, un punto fijo. Por eso, cuando la semilla
es 0, el código la reemplaza por `0x9E3779B9`, que es el entero más cercano a
2³² dividido por el número áureo φ —una constante *nothing-up-my-sleeve* que está
en `include/game_logic.hpp:45`.

### Qué ampliar si preguntan

- El **sesgo del módulo**: `next_range(n)` hace `next() % n` (`:58-61`). Como
  xorshift32 nunca emite 0, su salida en un periodo es {1,…,2³²−1}, y para `n=5`
  (los cinco LEDs) `2³²−1 = 5·858 993 459` exacto → **sesgo cero**.
- La **siembra** (`src/game_logic.cpp:406`): `seed=0` significa "usá entropía del
  hardware" (`get_rand_32()`); `seed≠0` significa "secuencia fija y repetible".
- La semilla se **persiste en Flash** (`:404`, `last_seed`) para que el último
  valor quede registrado.
- No es criptográfico: por ser lineal es **predecible** si ves suficientes
  salidas; sirve para jugabilidad, no para secretos.

### Pregunta probable + respuesta

**P:** ¿Qué pasa exactamente si pongo semilla 0?
**R:** Si no se corrigiera, el estado 0 quedaría clavado para siempre y el
generador daría 0 en cada llamada —un generador muerto. Por eso `set_seed(0)` lo
sustituye por `0x9E3779B9`. O sea: en mi diseño, **semilla 0 significa
"aleatoria"**, y nunca dejo que el estado interno sea 0.

---

## 8. Simón Dice

### Guión hablado

Simón Dice es el modo que más estira el mecanismo. La idea clásica: el sistema
muestra una secuencia de luces y sonidos, y el jugador la reproduce; cada acierto
agrega un elemento. Acá arranca con **longitud 1** (`:409-411`), muestra **toda**
la secuencia con `simon_show_sequence` (`:465-474`) —con LED y tono, y la
velocidad acelera a medida que crece—, y después valida entrada por entrada en
`simon_wait_input` (`:477-498`), comparando `btn == expected`.

Si acertás todo, la secuencia crece hasta un máximo de **64** (`SIMON_MAX_SEQ`,
`:522-528`); si fallás, es `game_over` y se reinicia a longitud 1 (`:511-519`). Y
como la secuencia sale del **xorshift32 con semilla fija**, dos personas pueden
jugar *exactamente* la misma secuencia —eso es lo que hace justa una competencia.

### Qué ampliar si preguntan

- La comparación `btn == expected` es el mismo índice LED↔botón que uso en todo el
  juego (LED1↔pulsador1, etc.), definido en `pin_defs.hpp` y mapeado en
  `game_logic.cpp:62-63`.
- El tono del buzzer acompaña cada elemento de la secuencia, lo que refuerza la
  memoria auditiva además de la visual.
- El reinicio a longitud 1 tras el fallo cierra el ciclo sin salir del modo: el
  jugador ve el game over y puede reintentar de inmediato.

### Pregunta probable + respuesta

**P:** ¿Cómo garantizás que la secuencia de Simón sea "justa" entre jugadores?
**R:** Porque no uso aleatoriedad de hardware: uso el **xorshift32 sembrado con un
valor fijo**. Misma semilla → misma secuencia de `next()` → misma secuencia de
LEDs. Así dos personas, o dos intentos de la misma persona, se enfrentan a la
misma dificultad exacta.

---

## 9. Precisión y timing (por qué µs)

### Guión hablado

Acá cierro la parte de precisión con una idea sola: **dónde** se toma el
timestamp importa tanto como el reloj que usás. El contador `time_us_32()` tiene
resolución de 1 µs, pero si yo leyera ese contador cuando el bucle principal se
entera de la pulsación, el scheduler ya me habría metido latencia variable.

Por eso el timestamp se captura **dentro de la interrupción del PIO**, en el
primer flanco del botón (`:167-185`). El PIO además hace el **debounce por
hardware** —una state machine por botón con un lockout de unos 5 ms—, así que ni
la CPU ni las IRQ se gastan filtrando rebotes. El resultado es una medición
estable, del orden del microsegundo, sin importar qué esté haciendo la otra tarea.

### Qué ampliar si preguntan

- `time_us_32()` da la vuelta cada 2³² µs ≈ **71,58 minutos**; la resta sin signo
  ya explicada hace que el wrap sea transparente.
- El debounce en hardware es **determinista**: no depende de cuándo el scheduler
  atiende una IRQ de GPIO, como pasaría con un debounce por software.
- Al no usar `sleep_ms` para el debounce, el núcleo queda libre para el resto del
  juego.

### Pregunta probable + respuesta

**P:** ¿Qué latencia real tenés entre el flanco físico y la captura del tiempo?
**R:** La interrupción del PIO se dispara en el mismo ciclo del primer flanco y lo
primero que hace el handler es leer `time_us_32()`. La latencia es de **pocos
ciclos de reloj** —microsegundos como mucho—, y sobre todo es *constante*, que es
lo que importa para comparar reacciones.

---

## 10. Red + OTA

> **Filmina con diagrama:** el "Flujo de red (Core 1)". Recorrerlo por capas:
> abajo el chip CYW43/Wi-Fi, encima lwIP (TCP/UDP), y sobre eso los servicios
> HTTP, DNS del captive portal, DHCP y mDNS; terminar en la flecha de OTA.

### Guión hablado

La parte de red corre en el núcleo 1. Tengo un **servidor web sobre la API raw de
lwIP** —sin sistema operativo de red, yo manejo los sockets—, Wi-Fi en modo
**estación** con respaldo a **punto de acceso**: si la estación pierde el enlace,
reintenta tres veces con backoff creciente y cae a un AP propio para que siempre
puedas entrar a configurar.

Arriba de eso hay un **captive portal** (responde el DNS y te lleva a la página),
**DHCP** para darle IP a quien se conecte al AP, y **mDNS** para que en la red
local el dispositivo aparezca como `picoreflex.local`. Y la guinda es la
**actualización OTA A/B**: subís un binario por HTTP, se verifica con CRC32, se
graba en un slot de staging, y si el firmware nuevo no arranca, hay **rollback**
automático al anterior.

### Qué ampliar si preguntan

- **Seguridad**: los endpoints de control exigen un **PIN** (`WEB_PIN`, por
  defecto `1234`) por cookie o cabecera `Authorization`, con **rate-limiting**
  de 60 peticiones/segundo y 5 intentos de login/segundo contra fuerza bruta.
- El mapa de Flash OTA: bootloader en `0x10000000`, aplicación en `0x10010000`,
  staging y metadatos con commit atómico.
- El navegador hace el OTA por HTTPS hacia el servidor de actualización; el
  firmware en sí se transfiere y verifica con CRC32 antes de conmutar.

### Pregunta probable + respuesta

**P:** ¿Qué pasa si la actualización OTA se corta a la mitad?
**R:** Nada grave: la imagen nueva se graba completa en el **slot de staging** y
solo se conmuta como "buena" cuando el CRC32 verifica. Si se corta antes, la
imagen activa queda intacta. Si se conmuta y la nueva no arranca, el bootloader
detecta el fallo y hace **rollback** a la anterior. Es el patrón A/B clásico.

---

## 11. Hardware

> **Filmina con diagrama:** el "Esquemático". Recorrerlo por bloques: alimentación
> (batería → divisor → ADC), entradas (botones → PIO), salidas (LEDs, buzzer PWM,
> OLED I2C), y el CYW43 del Wi-Fi.

### Guión hablado

El hardware es sencillo pero cada pieza está justificada. El **display OLED**
soporta dos controladores, SSD1306 y SH1106, por I2C en la dirección 0x3C/0x3D, y
el controlador se elige en runtime y se persiste. Los **botones** pasan por el
PIO, que como dije hace el debounce en hardware. El **buzzer** es pasivo y lo
manejo por PWM —la frecuencia sale de `clk_sys / (clkdiv·(wrap+1))`—. La
**batería** se mide por un divisor resistivo en el ADC de GPIO26, y la convierto a
porcentaje con una **curva de descarga calibrable** de tres puntos. Y todo está
protegido por un **watchdog hardware** de 8 segundos que ambos núcleos refrescan.

### Qué ampliar si preguntan

- El SH1106 difiere del SSD1306 en un offset de columna y en la configuración de
  pines COM; por eso el driver inicializa distinto según el controlador elegido.
- La curva de batería usa 4200/3700/3000 mV (100/50/0 %) con interpolación lineal
  y una compensación por carga.
- El watchdog, además de reiniciar, permite distinguir en el arranque si el último
  reset fue por watchdog (`watchdog_caused_reboot()`).

### Pregunta probable + respuesta

**P:** ¿Por qué medís la batería por GPIO26 y no por el pin VSYS?
**R:** Porque en la Pico W el GPIO29/VSYS está ocupado por el SPI interno del
módulo Wi-Fi y por el divisor interno VSYS/3. Por eso uso **GPIO26 (ADC0)** con un
divisor resistivo externo de 10 kΩ/22 kΩ, que además me deja medir una LiPo de
hasta 4,2 V dentro del rango del ADC.

---

## 12. Cierre — qué van a preguntar

### Guión hablado

Para cerrar: lo más importante de este trabajo no es que "ande", sino **por qué
anda así**. Que el tiempo se mida en microsegundos y que el wrap del contador se
absorba con una resta sin signo; que la semilla use un xorshift32 cuyo periodo es
2³²−1 y cuyo único estado muerto, el cero, se reemplaza por la razón áurea; que
dos tareas convivan en dos núcleos sin pisarse; y que el firmware se pueda
actualizar por aire sin riesgo de dejarlo ladrillo.

Esas cuatro cosas —**timing, seed, concurrencia y OTA**— resumen lo que aprendí en
la materia y lo que quiero defender hoy.

### Qué ampliar si preguntan

- "¿Por qué 13/17/5?" → porque es la terna de Marsaglia que da periodo máximo.
- "¿Semilla 0?" → punto fijo; se reemplaza por `0x9E3779B9`.
- "¿Reproducible?" → misma semilla = misma secuencia de LEDs/Simón.

### Pregunta probable + respuesta

**P:** Si tuvieras que mejorar una sola cosa, ¿cuál sería?
**R:** Le pondría **TLS al panel web** con `pico_mbedtls`. Hoy el HTTP viaja en
claro y la autenticación por PIN depende de la red; cifrar el canal cerraría el
eslabón más débil sin cambiar el diseño.

---

## Cierre final (discurso de 30–60 segundos)

Para terminar: PicoReflex empezó como "un juego de luces" y terminó siendo un
ejercicio completo de sistemas embebidos. Detrás de cinco LEDs hay una máquina de
estados, un contador de microsegundos, un generador pseudoaleatorio con la
matemática de GF(2), dos núcleos coordinados por FreeRTOS, una red completa con
portal cautivo y DNS, y una actualización OTA con rollback.

Lo que me llevo es una convicción: en embebido, la diferencia entre "funciona" y
"está bien hecho" está en los detalles que no se ven —dónde tomás el timestamp,
qué hacés cuando un contador de 32 bits se desborda, o por qué una semilla en
cero te puede matar el generador. Esos son los detalles que intenté atacar uno
por uno, y los que estoy en condiciones de defender.

Muchas gracias. Quedo a disposición para las preguntas.

---

## Preguntas trampa probables + respuestas preparadas

### 1. "Tu `reaction_us` es una resta sin signo. ¿No te da mal si el contador desborda justo en el medio?"

No, justamente es al revés: la resta **sin signo** es lo que lo hace correcto.
Como el intervalo entre `t0` y la pulsación es siempre menor que 2³² µs, la
aritmética módulo 2³² del `uint32_t` devuelve exactamente el tiempo transcurrido,
sin importar si el contador pasó por cero en el medio. Si hubiera usado una resta
con signo, ahí sí tendría un número negativo absurdo.

### 2. "Si xorshift32 es lineal, ¿no es predecible? ¿No es un problema?"

Sí, es lineal y predecible —y por eso **no es criptográfico**. Pero acá no estoy
generando secretos: estoy generando una secuencia de luces **reproducible**. La
predecibilidad es una *feature* en este caso, no un bug: es lo que permite que dos
jugadores compitan con la misma secuencia. Si mañana necesitara aleatoriedad
segura, usaría un CSPRNG del SDK, no xorshift32.

### 3. "¿Por qué no medís con `sleep_us` o un timer de FreeRTOS?"

Porque `sleep_us`/`sleep_ms` y los timers de FreeRTOS tienen granularidad y
latencia de *scheduling*: te despiertan cuando el scheduler decide, no cuando
ocurrió el evento. Yo necesito el instante del **flanco físico**, y eso solo lo da
leer `time_us_32()` dentro de la interrupción del PIO.

### 4. "Decís que el debounce es por PIO. ¿Qué gana frente a un debounce por software?"

Determinismo y CPU cero. El PIO muestrea a 1 MHz y aplica un lockout de rebote sin
que ningún núcleo haga nada; el software solo recibe una interrupción por flanco
ya limpio. Con debounce por software, la calidad depende de cuándo el scheduler te
atiende la IRQ, y consumís ciclos de CPU que el juego podría usar.

### 5. "La semilla se persiste en Flash. ¿No desgastás la Flash escribiendo a cada partida?"

No, porque la persistencia no es por partida: se escribe cuando la configuración
cambia o al guardar récords, con `flash_safe_execute` y un checksum. La semilla de
la partida se lee de la configuración en RAM; la Flash solo se toca en el guardado,
que es poco frecuente y está acotado a un sector reservado.
