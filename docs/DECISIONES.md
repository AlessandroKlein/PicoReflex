# Decisiones de Diseño — PicoReflex

> Cada decisión técnica se justifica y se vincula a la documentación oficial del
> Raspberry Pi Pico SDK. Los enlaces apuntan a la documentación generada por
> Doxygen (índice) y a las páginas temáticas oficiales.

Documentación oficial de referencia (carpeta raíz):

- [Índice Doxygen del Pico SDK](https://www.raspberrypi.com/documentation/pico-sdk/index_doxygen.html)
- [Hardware APIs](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html)
- [High Level APIs](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html)
- [Third-party libraries](https://www.raspberrypi.com/documentation/pico-sdk/third_party.html)
- [Networking](https://www.raspberrypi.com/documentation/pico-sdk/networking.html)
- [Runtime Infrastructure](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html)
- [Misc](https://www.raspberrypi.com/documentation/pico-sdk/misc.html)

---

## 1. Lenguaje y estándar: C++17

- **Decisión:** Código en C++17 (`.cpp`/`.hpp`), compilado con `arm-none-eabi-g++`.
- **Por qué:** La consigna (docs/Idea.md) pide "C/C++" y estructura el proyecto en
  `main.cpp` + módulos `.cpp`/`.hpp`. C++17 permite `enum class`, `static_assert`
  y estructuras limpias sin costo de runtime adicional.
- **Referencia:** [CMakeLists / pico_sdk_init en "Getting started"](https://www.raspberrypi.com/documentation/pico-sdk/index_doxygen.html)

---

## 2. Mapa de pines (GPIO)

- **Decisión:** Se respeta la asignación de docs/Idea.md §4:

  | Periférico | Pin | Detalle |
  |---|---|---|
  | LEDs (x5) | GPIO 2..6 | Salidas digitales |
  | Pulsadores (x5) | GPIO 7..11 | Entradas con pull-up + IRQ |
  | Display I2C | GPIO 14/15 (I2C1) | SSD1306 |
  | Buzzer pasivo | GPIO 16 | PWM |
  | Batería (ADC) | GPIO 26 (ADC0) | Divisor externo 10k/22k |
  | LED onboard | CYW43 | Indicador Wi-Fi |

- **Por qué:** En la RP2040, `GPIO14/15` pertenecen a **I2C1** (no I2C0), por lo
  que se usa `i2c1`. No hay conflicto entre LEDs (2–6), botones (7–11), buzzer
  (16) ni I2C (14/15).
- **Referencia:**
  - [hardware_gpio](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio)
  - [hardware_i2c](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_i2c)
  - [hardware_pwm](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pwm)
  - [hardware_adc](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc)

---

## 3. PRNG: determinista por semilla con `xorshift32` + `pico_rand`

- **Decisión:** El motor determinista es un `xorshift32` propio; el modo aleatorio
  siembra con `get_rand_32()` de `pico_rand`.
- **Por qué:** La API `pico_rand` ([pico/rand.h](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#pico_rand))
  **no expone una función para fijar la semilla** (se auto-siembra con ROSC,
  timer, RAM hash, etc.). Para lograr una secuencia **idéntica en cada reinicio**
  con `SEED = 12345`, se implementa un generador software trivial y sin estado
  oculto (Marsaglia, "Xorshift RNGs", 2003). La propia docs/Idea.md acepta
  "pico_rand **o** xorshift/LCG en C++".
- **Referencia:**
  - [pico_rand](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#pico_rand)
  - `get_rand_32()` en `src/rp2_common/pico_rand/include/pico/rand.h`

---

## 4. Temporización de reacción en microsegundos

- **Decisión:** El timestamp se captura con `time_us_32()` **dentro de la
  interrupción GPIO** (flanco de bajada del pulsador). La resta
  `t_final - t_inicial` en aritmética sin signo de 32 bits maneja el "wrap"
  (~71.6 min) sin código especial.
- **Por qué:** Capturar el contador en la propia IRQ minimiza la latencia y el
  jitter; `time_us_32()` lee directamente el registro `TIMELR` del temporizador
  hardware, con resolución de 1 µs.
- **Referencia:**
  - [hardware_timer / time_us_32](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_timer)
  - [hardware_gpio / gpio_set_irq_enabled_with_callback](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio)

---

## 5. DMA: "trace" hardware de los pulsadores

- **Decisión:** Se usa **DMA con buffer en anillo** (`channel_config_set_ring`),
  rítmado por un **slice PWM** como fuente de DREQ (`channel_config_set_dreq`),
  para muestrear continuamente el registro `sio_hw->gpio_in` (estado crudo de los
  5 pulsadores) **sin intervención de la CPU**.
- **Por qué / aclaración técnica:** DMA *mueve* datos, no "mide" tiempo. La
  medición de reacción en µs la aporta `time_us_32()` (punto §4). El DMA cumple
  aquí el rol de *registrador ultrasensible por hardware*: captura el estado de
  los botones a ~2 kHz de forma automática, liberando a la CPU (ver también
  docs/DUDAS.md §5).
- **Referencia:**
  - [hardware_dma](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_dma)
  - `channel_config_set_ring` / `channel_config_set_dreq` en
    `src/rp2_common/hardware_dma/include/hardware/dma.h`

---

## 6. Dual-core (`pico_multicore`)

- **Decisión:** `multicore_launch_core1()` arranca el Core 1.
  - **Core 0:** juego, IRQ de botones, PWM (buzzer), DMA, Flash.
  - **Core 1:** Wi-Fi (`cyw43_arch`), servidor HTTP (lwIP), display OLED, ADC de
    batería.
- **Comunicación inter-núcleo:** `critical_section_t` (pico/sync.h) para el
  estado compartido y `queue_t` (pico/util/queue.h) para los eventos de control
  Core1→Core0. **No** se usan los FIFOs crudos: la propia documentación de
  `pico_multicore` recomienda usar una cola en lugar de los FIFOs.
- **Referencia:**
  - [pico_multicore](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_multicore)
  - [queue (pico/util/queue.h)](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#group_queue)
  - [pico_sync](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_sync)

---

## 7. Wi-Fi: `cyw43_arch` en modo *sys_freertos* (tarea `tcpip`)

- **Decisión:** Se enlaza `pico_cyw43_arch_lwip_sys_freertos`, que atiende el
  driver CYW43 y lwIP en una **tarea dedicada** (`tcpip`) sobre FreeRTOS, accesible
  desde **ambos núcleos**.
- **Conectividad:** STA (10 s de timeout) → si falla, SoftAP
  (`PicoW-ReactionGame` / clave por defecto `12345678` —editable desde `/wifi`
  (8..15 chars) y persistida en Flash—, IP `192.168.4.1`, **WPA2 mixto**,
  **canal 1 fijo**). La clave se resuelve en `ap_start()`
  (`src/web_server.cpp:1067-1083`): si `ap_password` está vacío se usa
  `AP_PASSWORD_DEFAULT` (`include/config.hpp`). El LED onboard queda fijo en STA y
  parpadea en AP. El DHCP del AP lo provee el servidor integrado (ver §21).
- **Referencia:**
  - [pico_cyw43_arch](https://www.raspberrypi.com/documentation/pico-sdk/networking.html#pico_cyw43_arch)
  - `cyw43_arch_wifi_connect_timeout_ms`, `cyw43_arch_enable_ap_mode` en
    `pico/cyw43_arch.h`

---

## 8. Servidor web: lwIP (API raw) + NO_SYS=0 (con FreeRTOS)

- **Decisión:** Servidor HTTP con la **API raw de lwIP** (`tcp_new`, `tcp_bind`,
  `tcp_listen`, `tcp_accept`, `tcp_recv`, `tcp_write`, `tcp_output`) en modo
  `NO_SYS=0` (con sistema operativo FreeRTOS).
- **Por qué:** Con FreeRTOS, lwIP se atiende en la tarea `tcpip` que
  `pico_cyw43_arch_lwip_sys_freertos` gestiona automáticamente (ver §30). La API
  raw se mantiene; solo cambia el modelo de ejecución.
- **Configuración lwIP:** `lwipopts.h` con `MEM_SIZE=16000`, `PBUF_POOL_SIZE=32`
  y `TCP_SND_BUF=(8*TCP_MSS)`. El buffer de respuesta HTTP es `g_body[8192]`
  (la Home con el CSS nuevo mide ~4.4 KB) y el **stack del Core 1 se amplió a
  4 KB** (`PICO_CORE1_STACK_SIZE=0x1000`), ver §24.
- **Referencia:**
  - [lwIP raw TCP API](https://www.nongnu.org/lwip/2_1_x/group__tcp__raw.html)
  - [Networking (pico-sdk)](https://www.raspberrypi.com/documentation/pico-sdk/networking.html)

---

## 9. Medición de batería (ADC + divisor resistivo externo)

- **Decisión:** Se mide la batería con un **divisor resistivo EXTERNO** en
  **GPIO26 (ADC0)** — libre en la Pico W — con `R1 = 10 kΩ` (batería → pin ADC) y
  `R2 = 22 kΩ` (pin ADC → GND).
- **Por qué:** usar GPIO29 (ADC3) en la Pico W no es viable para un divisor
  externo: ese pin está ocupado por el SPI del Wi-Fi (`WL_CLK`) y por el divisor
  interno VSYS/3. GPIO26 evita ese conflicto por completo (no se necesita el lock
  del CYW43).
- **Conversión:** `Vpin = adc/4095 · 3.3 V`, y
  `Vbat = Vpin · (R1 + R2) / R2 = Vpin · 32 / 22`. Porcentaje por curva LiPo 1S
  (4.2 V = 100 %, 3.7 V = 50 %, 3.0 V = 0 %). Con 4.2 V en batería, el pin recibe
  2.89 V (< 3.3 V de referencia, sin recorte).
- **Referencia:**
  - [hardware_adc](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc)

---

## 10. Almacenamiento no volátil (Flash)

- **Decisión:** Se reserva el **último sector** de la Flash (2 MB → `0x1FF000`).
  Escritura con `flash_range_erase()` (4096 B) + `flash_range_program()` (256 B)
  dentro de `flash_safe_execute()`.
- **Por qué:** `flash_safe_execute()` (pico_flash) deshabilita IRQs
  (`save_and_disable_interrupts`) y bloquea al otro núcleo (`multicore_lockout`)
  para garantizar que no se ejecute código desde Flash durante el
  borrado/programación. Ambos núcleos llaman `flash_safe_execute_core_init()` al
  arrancar. Es exactamente la secuencia `multicore_lockout_start()` +
  `save_and_disable_interrupts()` que exige la cátedra, encapsulada por el SDK.
- **Referencia:**
  - [hardware_flash](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash)
  - [pico_flash / flash_safe_execute](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_flash)

---

## 11. Display OLED SSD1306 (I2C1)

- **Decisión:** Driver SSD1306 (128×64) con framebuffer de 1024 bytes y texto
  5×7. Se usa **modo de direccionamiento horizontal** y un solo volcado del
  framebuffer.
- **Por qué:** El SSD1306 muestra más información (tiempo, semilla, modo, batería,
  IP) que un LCD1602. I2C1 en 400 kHz.
- **Referencia:**
  - [hardware_i2c](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_i2c)
  - Datasheet SSD1306 (Solomon Systech) — secuencia de inicialización.
- **Nota (v0.7.0):** la decisión se extendió para soportar también **SH1106**
  (OLED 1.3", RAM 132×64, offset de columna +2): el controlador se elige en
  runtime desde `/display` (`?controller=0|1`) y se persiste en Flash.

---

## 12. Buzzer PWM

- **Decisión:** Tonos generando una onda cuadrada con un slice PWM: `wrap` fija la
  frecuencia (`f = clk_sys / (clkdiv · (wrap+1))`) y el nivel del canal fija el
  duty (50 %). Divisor fijo `AUDIO_CLKDIV = 100`.
- **Referencia:**
  - [hardware_pwm](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pwm)
  - [hardware_clocks / clock_get_hz](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_clocks)

---

## 13. Bajo consumo (DORMANT, < 2 mA)

- **Decisión:** Tras 3 min sin actividad, el Core 0 ejecuta un protocolo con el
  Core 1 y entra en modo **DORMANT** (`low_power_dormant_until_gpio_pin_state`)
  con `DORMANT_CLOCK_SOURCE_ROSC`, despertando por **cualquier pulsador**.
- **Protocolo (dual-core):**
  1. Core 0 activa `g_sleep_request`.
  2. Core 1 apaga la radio con `cyw43_arch_deinit()` y responde `g_sleep_ready`.
  3. Core 0 habilita el *wake-up* GPIO en los 5 pulsadores
     (`gpio_set_dormant_irq_enabled`) y entra en DORMANT.
  4. Al despertar, el SDK restaura los relojes automáticamente
     (`low_power_wake_from_dormant` → `runtime_init_clocks`); Core 0 libera
     `g_sleep_request` y Core 1 re-inicializa con `cyw43_arch_init()`.
- **Por qué:** el modo *sleep* (~7–9 mA) no cumple "< 2 mA"; *dormant* (~0.95 mA
  en RP2040) sí. Es imprescindible apagar el CYW43 antes (si no, consume
  20–40 mA en reposo). Pendiente de verificación final en hardware (DUDAS.md §3).
- **Referencia:**
  - [pico_low_power](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_low_power)
  - `low_power_dormant_until_gpio_pin_state` y `DORMANT_CLOCK_SOURCE_ROSC` en
    `pico/low_power.h`

---

## 14. Salida estándar (stdio)

- **Decisión:** `stdio` por **USB** (`pico_enable_stdio_usb 1`) para depurar con
  `printf()` sobre el puerto USB; UART deshabilitado.
- **Referencia:**
  - [pico_stdio](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#pico_stdio)

---

## 15. Captive portal (DNS en modo Access Point)

- **Decisión:** En modo SoftAP se levanta un **servidor DNS mínimo** (API raw UDP
  de lwIP, puerto 53) que responde a toda consulta con la IP `192.168.4.1`
  (registro A). Así el navegador del usuario es redirigido a la página de
  configuración sin importar la URL que escriba.
- **Por qué:** los dispositivos modernos validan la conexión con un "captive
  portal check" (resuelven un dominio de prueba). Responder el DNS con la IP
  local captura ese chequeo y muestra la web embebida. Es el complemento natural
  del **servidor DHCP integrado** (`src/dhcpserver.c`, ver §21) que asigna IP a
  los clientes del AP.
- **Referencia:**
  - [lwIP raw UDP API](https://www.nongnu.org/lwip/2_1_x/group__udp__raw.html)
  - Formato DNS: RFC 1035.

---

## 16. Disparador de inicio: dos botones simultáneos + cuenta regresiva

- **Decisión:** la ronda **no** arranca con un solo botón. Se requiere presionar
  **dos botones a la vez** (disparador de inicio) y, tras eso, se ejecuta una
  **cuenta regresiva** de `start_delay_s` segundos (configurable vía web, por
  defecto 3 s, límite 10 s) con *beeps*, antes de encender el primer LED. El
  inicio desde la web (`/start`) pasa por la misma cuenta regresiva.
- **Por qué:** evita arranques accidentales por una pulsación suelta y da al
  jugador un "3, 2, 1" audible para prepararse, lo que hace la medición más
  justa y reproducible en competencias estandarizadas.
- **Referencia:**
  - [hardware_gpio](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio)
    (lectura de múltiples pulsadores simultáneos)
  - [hardware_timer](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_timer)
    (temporización de la cuenta regresiva)
  - [hardware_pwm](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pwm)
    (beeps del buzzer)

---

## 17. Separación frontend / backend de la web

- **Decisión:** la web se separa en dos módulos: el **backend** de ruteo y parseo
  en `src/web_server.cpp` (lwIP + `handle_config`) y el **frontend** en
  `src/web_ui.cpp` / `include/web_ui.hpp`, con la interfaz pura
  `web_ui::render(path, out, len)` que **solo lee** el estado y produce HTML, sin
  modificar nada.
- **Por qué:** desacopla la lógica de red (qué ruta, qué parámetro) de la
  presentación (HTML), lo que facilita mantener cada página y reutilizar el
  escape de HTML (`web_ui::html_escape`) en un único lugar. Cada página tiene su
  formulario y su ruta propios (`/`, `/seed`, `/game`, `/display`, `/wifi`).
  La página `/player` fue eliminada en v0.4.0 (sin multijugador).
- **Referencia:**
  - [lwIP raw TCP API](https://www.nongnu.org/lwip/2_1_x/group__tcp__raw.html)
    (backend HTTP)
  - Módulos `web_server` / `web_ui` en `include/` y `src/`.

---

## 18. OLED deshabilitable (`oled_enabled`)

- **Decisión:** el display se puede apagar/encender desde la web (`/display`,
  campo `oled_enabled` en `GameConfig`) con `display::set_power(false)`, que envía
  el comando SSD1306 **0xAE** (display off); para encenderlo, **0xAF**.
- **Por qué:** toda la información también está disponible en la web, así que el
  OLED puede apagarse para **ahorrar energía** (especialmente útil alimentado por
  batería) sin perder funcionalidad.
- **Referencia:**
  - [hardware_i2c](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_i2c)
  - Datasheet SSD1306 (Solomon Systech) — comandos 0xAE / 0xAF.

---

## 19. Wi-Fi configurable desde la web y persistido

- **Decisión:** las credenciales STA se guardan en `GameConfig`
  (`wifi_ssid[33]` / `wifi_password[65]`, con defaults `WIFI_SSID_DEFAULT` /
  `WIFI_PASSWORD_DEFAULT`), se editan desde `/wifi`, se **persisten en Flash**
  (`PersistentData` v2) y se aplican re-conectando automáticamente (bandera
  `g_wifi_reconnect` en el bucle del Core 1).
- **Por qué:** permite cambiar de red **sin recompilar ni flashear**; el
  dispositivo queda configurable en campo (mismo criterio que el resto de la
  configuración).
- **Referencia:**
  - [pico_cyw43_arch / cyw43_arch_wifi_connect_timeout_ms](https://www.raspberrypi.com/documentation/pico-sdk/networking.html#pico_cyw43_arch)
  - [hardware_flash](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash)
  - [pico_flash / flash_safe_execute](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_flash)

---

## 20. Historial de tiempos de reacción

- **Decisión:** `GameStats` guarda un **historial de los últimos 20 tiempos**
  (`history[20]` en ms, `history[0]` = más reciente, más `history_count`), que la
  página Home muestra como tabla.
- **Por qué:** además del promedio y el mejor tiempo, el usuario puede ver la
  evolución de sus reacciones y detectar tendencias (fatiga, mejora), enriqueciendo
  el panel sin tocar el motor del juego.
- **Referencia:**
  - [pico_sync (critical_section)](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_sync)
    para proteger el acceso al historial entre núcleos.

## 21. Servidor DHCP en modo Access Point (fix de "conecta y se desconecta")

- **Decisión:** se integró un **servidor DHCP** (`src/dhcpserver.c`, código MIT de
  MicroPython vía [pico-examples](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/access_point))
  que asigna IPs `192.168.4.16`–`192.168.4.23` a los clientes del AP.
- **Por qué (causa raíz):** el CYW43 **no trae servidor DHCP en modo AP** (la opción
  `CYW43_NETUTILS` está desactivada por defecto). Sin DHCP, el teléfono se asociaba
  a `PicoW-ReactionGame` pero **nunca recibía IP**, por eso "conectaba y se
  desconectaba". El ejemplo oficial lo deja explícito: `dhcp_server_init(...)`.
- **Nota de seguridad:** entre v0.2.5 y v0.4.3 el AP se dejó **abierto** (sin
  clave) para esquivar un problema de compatibilidad WPA2 con algunos teléfonos.
  En **v0.4.4 se volvió a WPA2** (clave `12345678`): el problema real era el DHCP
  (ya resuelto), no la autenticación. Se controla con `AP_OPEN` en
  `src/web_server.cpp`.

## 22. DORMANT condicionado al estado del Wi-Fi

- **Decisión:** el modo DORMANT (bajo consumo, < 2 mA) **solo se activa si el Wi-Fi
  está apagado** (sin STA ni AP). Si la red está activa, el dispositivo permanece
  despierto.
- **Por qué:** el DORMANT apaga el Wi-Fi (handshake `g_sleep_request`/`g_sleep_ready`
  → `cyw43_arch_deinit`), dejando el servidor web inaccesible ("tardó demasiado en
  responder"). Como la web de configuración necesita la red siempre viva, se evitó
  el conflicto condicionando el sueño al estado de red.

## 23. FreeRTOS: integrado (SMP, 2 núcleos)

- **Decisión:** se integró **FreeRTOS SMP** en v0.5.0 (`game_task` + `network_task`
  con `vTaskStartScheduler()`). El primer intento (v0.3.0) falló y se revirtió; la
  causa real fue el stack/patrón, no la ubicación de `cyw43_arch_init()`.
- **Patrón correcto:** `cyw43_arch_init()` se llama **dentro de la tarea de red**
  (`web::init()`), como en los ejemplos oficiales `pico_w/wifi/freertos` (NO en
  `main()` antes del scheduler). Ver [`Freertos.md`](Freertos.md).

## 24. Stack del Core 1 ampliado a 4 KB (fix de *hard fault*)

- **Decisión:** `PICO_CORE1_STACK_SIZE` se sube de 2 KB (0x800, valor por defecto)
  a **4 KB** (0x1000) con `add_compile_definitions(PICO_CORE1_STACK_SIZE=0x1000)`
  en `CMakeLists.txt`.
- **Por qué:** el Core 1 aloja el servidor web (`cyw43_arch` + lwIP + HTTP). Con
  2 KB, al reconectar el Wi-Fi (`cyw43_arch_deinit/init`) o al escanear, el stack
  **desbordaba** y el dispositivo se reiniciaba solo (*hard fault*). 4 KB es el
  máximo que cabe en `SCRATCH_X` junto al stack del Core 0.
- **Referencia:**
  - [pico_multicore (PICO_CORE1_STACK_SIZE)](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_multicore)

## 25. `url_decode` corta en `&` (fix del guardado de Wi-Fi)

- **Decisión:** `url_decode()` (en `src/web_server.cpp`) se modifica para que
  **termine en `&`** (separador de parámetros), no solo en `\0`.
- **Por qué:** antes copiaba el valor de un parámetro hasta el final de la query,
  así que al guardar `ssid=MiRed&pass=clave`, el SSID quedaba como
  `"MiRed&pass=clave"` (todo junto) y la reconexión fallaba. Ahora cada parámetro
  se decodifica solo hasta su propio `&`.
- **Referencia:** formato de query string (RFC 3986, separador `&`).

## 26. Escaneo de redes Wi-Fi: retirado del frontend

- **Decisión:** el endpoint `/scan` (backend) se mantiene **reservado/interno**,
  pero el **botón "Buscar redes"** (y su JavaScript/datalist) se **elimina** de
  `/wifi` en v0.4.3.
- **Por qué:** el escaneo del CYW43 (`cyw43_wifi_scan`) no devolvía redes de forma
  fiable en el modo *threadsafe background* (y menos en modo AP), así que no
  aportaba valor. Se prefiere que el usuario escriba el SSID a mano.

## 27. Modo Simón Dice (secuencia de memoria creciente)

- **Decisión:** se agrega `GameMode::SIMON_SAYS` (v0.6.0): una **secuencia de LEDs
  que crece** (validación clásica de "Simon Says").
  - Correcto: se agrega un elemento aleatorio al final y se repite la secuencia.
  - Incorrecto (o timeout): Game Over + señal de error + reinicio a longitud 1.
- **Velocidad según dificultad:** la presentación va de 700 ms (Fácil) a 300 ms
  (Difícil); cada LED tiene su **tono** (escala pentatónica).
- **Semilla determinista:** la secuencia la genera el mismo `xorshift32` sembrado
  con la semilla, así que **una misma semilla produce siempre la misma secuencia**
  (útil para practicar/aprender patrones y medir progreso). Con `seed=0` es
  aleatoria (entropía del hardware).
- **Referencia:** el juego clásico "Simon Says" (secuencia + memoria + velocidad).

## 28. LEDs: abstracción GPIO / WS2812 (NeoPixel)

- **Decisión:** se crea el módulo `led_driver` con DOS implementaciones
  seleccionables en compilación con la macro `PICOREFLEX_WS2812`:
  - `0` (por defecto): 5 LEDs GPIO con resistencia (encendido simple).
  - `1`: WS2812 (NeoPixel) por **PIO** en GPIO28, con color por LED y **efectos de
    luces** al ganar (verde) y perder (rojo).
- **Por qué:** permite reemplazar los LEDs discretos por una tira direccionable sin
  tocar el motor del juego (la interfaz `led::` es la misma). Referencia del PIO:
  [pico-examples/pio/ws2812](https://github.com/raspberrypi/pico-examples/tree/master/pio/ws2812).

## 29. Linker script autocontenido (fix de `memmap_default.incl` en Windows)

- **Problema:** en Windows, el driver `g++` de la toolchain ARM **descarta las
  flags `-L`** (`-Wl,-L`, `-Xlinker -L` o `@rsp`) cuando los objetos se pasan por
  fichero de respuesta `@rsp` (lo que hace Ninja al enlazar cientos de objetos).
  `ld` usa esas `-L` para resolver las directivas `INCLUDE "..."` del linker
  script del SDK (`memmap_default.ld` → `memmap_default.incl` → …), así que el
  enlace falla con `cannot open linker script file memmap_default.incl`. En Linux
  no ocurre (g++ conserva `-L`), por eso el CI pasaba y el build local no.
- **Decisión:** generar, por target, un **linker script autocontenido** donde todas
  las directivas `INCLUDE "..."` quedan resueltas e insertadas en línea, y pasarlo
  a `ld` por `-T` (ruta absoluta, que no depende de `-L`). Lo genera
  `tools/generate_linker_script.py` en tiempo de configuración (ver
  `picoreflex_self_contained_linker_script` en `CMakeLists.txt`), replicando el
  orden de búsqueda de `-L`: rutas de override primero y luego `script_include`.
  El script se escribe en `build/<target>_memmap.ld` y se inyecta mediante la
  propiedad `PICO_TARGET_LINKER_SCRIPT` del SDK.
- **Detalle:** el generador es consciente de comentarios `/* … */`; los `INCLUDE`
  que aparecen *dentro* de un comentario (p. ej. el ejemplo de `memory_extra.incl`)
  se conservan tal cual, igual que hace `ld`, para no romper el anidado de bloques.
- **Consecuencia:** el mapa de memoria (app en `0x10010000`, bootloader en
  `0x10000000`) queda idéntico al del mecanismo previo de override por `-L`; solo
  cambia *cómo* se resuelven los `INCLUDE`.

## 30. Fix del cuelgue en `cyw43_arch_init()` (tarea tcpip nunca creada)

- **Síntoma:** la app se colgaba en `web::init()` justo después de imprimir
  `[web] cyw43_arch_init()...`; nunca aparecía `OK`. El LED Wi-Fi no encendía y
  no se levantaba el AP. Hardware, bootloader y linker quedaron descartados por
  bisect (blink en el slot de app `0x10010000` funciona).
- **Causa raíz:** con `pico_cyw43_arch_lwip_sys_freertos`, `cyw43_arch_init()`
  ejecuta `lwip_freertos_init()` → `tcpip_init()` → `sys_thread_new()` con
  `TCPIP_THREAD_STACKSIZE`. Nuestro `lwipopts.h` no definía ese macro, y el
  default de lwIP es `0` (`lwip/opt.h`), por lo que `xTaskCreate` recibía stack
  0 → la tarea `tcpip_thread` **nunca se creaba** → `xSemaphoreTake(init_sem,
  portMAX_DELAY)` en `lwip_freertos.c:36` bloqueaba para siempre.
- **Fix:** en `lwipopts.h`, mantener `NO_SYS 0` y añadir el bloque
  `#if !NO_SYS` con `TCPIP_THREAD_STACKSIZE 1024`,
  `DEFAULT_THREAD_STACKSIZE 1024`, `TCPIP_MBOX_SIZE 8` y los `*_RECVMBOX_SIZE 8`
  (valores del ejemplo oficial `pico_w/wifi/freertos/iperf`). Además se revirtió
  el cambio previo a `threadsafe_background` (que rompía el arranque por
  completo): en `CMakeLists.txt` de vuelta a `pico_cyw43_arch_lwip_sys_freertos`,
  y en `web_server.cpp`/`captive_portal.cpp` se retiraron los wrappers
  `cyw43_arch_lwip_begin/end` (innecesarios con `sys_freertos`).
- **Por qué no `threadsafe_background`:** es una variante *sin* tarea tcpip
  (`NO_SYS=1`, raw API con polling en IRQ de baja prioridad), pensada para
  bare-metal; con FreeRTOS el arranque se corrompía (ni siquiera imprimía
  `[main]`). `sys_freertos` es la variante correcta para este firmware.

---

## 31. Salto del bootloader a la app con IRQ habilitadas (fix "USB no reconocido")

- **Síntoma:** al arrancar la app detrás del bootloader, Windows mostraba
  "dispositivo USB no reconocido" (Code 43): sin puerto COM, LED onboard apagado,
  chip Wi-Fi frío, serial en blanco. El build standalone (app enlazada en
  `0x10000000`, sin bootloader) sí enumeraba el COM.
- **Causa raíz:** `jump_to_app()` (`src/bootloader.c`) hacía `cpsid i` (PRIMASK=1,
  IRQ enmascaradas) antes de saltar. La app (`main.cpp` → `stdio_init_all`)
  habilita el controlador USB y su IRQ, pero con PRIMASK=1 la IRQ USB (donde corre
  `tud_task` de TinyUSB) no puede disparar durante `stdio_init_all()`; el host ve
  el attach (pull-up) pero nunca recibe descriptor válido → Code 43.
- **Fix:** `jump_to_app()` replica ahora un reset limpio antes del salto:
  1. `nvic_hw->icer = 0xFFFFFFFF` (deshabilita las IRQ externas 0..31);
  2. `nvic_hw->icpr = 0xFFFFFFFF` (limpia las IRQ pendientes);
  3. `systick_hw->csr = 0` (apaga SysTick);
  4. `scb_hw->vtor = vt_addr` (tabla de vectores de la app);
  5. salto con `msr msp, vt[0]` + `cpsie i` (PRIMASK=0) + `dsb`/`isb` + `bx vt[1]`.
  Al limpiar el NVIC y apagar SysTick **antes** de `cpsie i`, no queda IRQ que
  pueda dispararse en la ventana entre el VTOR y el `runtime_init` de la app
  (solo HardFault/NMI no enmascarables, que vectorizan por la tabla válida de la
  app). `msr msp, vt[0]=0x20042000` replica la carga del SP que hace el reset por
  hardware; `VTOR=0x10010100` está alineado a 256 B (válido en Cortex-M0+).
- **Bootloader UART-only:** el bootloader usa `stdio_uart_init()` (sin USB) para
  que la app sea el único dueño del controlador USB; el salto se hace con el XIP
  ya configurado por el boot2 del propio bootloader (no hace falta ejecutar el
  `.boot2` de la app).

---

## 32. Modo dormir configurable (ahorro de energía)

- **Decisión:** el modo DORMANT deja de usar un umbral fijo de 3 minutos y pasa a
  ser **configurable** con dos campos en `GameConfig` (`include/config.hpp:160-164`):
  `sleep_enabled` (bool, por defecto `true`) e `idle_sleep_min` (uint32_t, por
  defecto 3, rango 1..60).
- **Persistencia:** ambos campos se guardan en Flash
  (`PersistentData::sleep_enabled` e `PersistentData::idle_sleep_min`,
  `include/flash_storage.hpp:69-70`). `PERSISTENT_VERSION` sube de 5 a 6
  (`include/flash_storage.hpp:36`), por lo que el primer arranque tras la
  actualización **resetea la configuración persistida**.
- **UI:** la página `/display` añade la card "Modo dormir (ahorro de energía)" con
  botones Habilitar/Deshabilitar y un campo "Inactividad (minutos)" 1..60
  (`src/web_ui.cpp:381-398`).
- **Comportamiento:** el protocolo de dormancia (`src/game_logic.cpp:565-579`) ahora
  respeta `cfg.sleep_enabled` y `cfg.idle_sleep_min` (antes era `IDLE_SLEEP_AFTER_US`
  fijo de 3 min). Solo dormita con el Wi-Fi apagado (sin STA ni AP, ver §22);
  `sleep_enabled=false` desactiva el sueño por completo.

---

## 33. Display OLED en negro: doble charge-pump + auto-test (0xA5)

- **Síntoma:** el OLED se detectaba en `0x3C` y el init imprimía
  `[display] controlador=…, init=OK`, pero el panel quedaba en negro (sin destello
  ni texto). La detección I2C **solo** confirma que la dirección responde (ACK):
  SSD1306 y SH1106 responden igual en `0x3C`, así que no distingue el controlador.
- **Causa raíz (software):** el init era correcto *por chip* pero mandaba **una sola**
  fuente de tensión según el controlador elegido (SSD1306 → charge-pump `0x8D,0x14`;
  SH1106 → DC-DC interno `0xAD,0x8B`). Si el panel real era el *otro* chip (muy común
  en módulos 1.3" económicos), su comando de bomba se ignoraba → píxeles sin tensión
  interna → negro total.
- **Fix (commit 827a328):** las DOS secuencias mandan AMBAS fuentes de tensión (cada
  una ignorada con seguridad por el chip que no la usa); pre-carga corregida a
  `0xD9,0x22` y VCOM a `0xDB,0x35` (valores de referencia); SH1106 añade `0x31`
  (tensión de bomba 7.4 V).
- **Auto-test de arranque (0xA5):** al final de `init()` se envía "Display All ON"
  (`0xA5`) durante 1 s y luego `0xA4`. Ese comando enciende TODOS los píxeles sin
  leer la RAM (independiente de direccionamiento/offset/contraste), de modo que un
  destello blanco demuestra que panel + charge-pump + alimentación funcionan. Log
  serial `[display] self-test: all-on rc=… (ACK/NACK)` para acotar el fallo.
- **Conclusión (hardware):** con `rc=1 (ACK)` y SIN destello, el fallo queda acotado
  al charge-pump/panel: el chip está vivo (responde a todo) pero el panel no emite
  luz — defecto físico del módulo (flex agrietado, bomba muerta), no reparable por
  software. **IMPORTANTE:** estos OLED 1.3" son de 3.3 V; alimentarlos desde `VBUS`
  (5 V) puede quemarlos y, además, sube sus pull-ups de I2C a 5 V sobre los
  GPIO14/15 (que **no son tolerantes a 5 V**). Conectar SIEMPRE `VCC → 3V3(OUT)`
  (ver `docs/ESQUEMATICO.md` §3.3).


