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
- **Por qué:** La consigna (Idea.md) pide "C/C++" y estructura el proyecto en
  `main.cpp` + módulos `.cpp`/`.hpp`. C++17 permite `enum class`, `static_assert`
  y estructuras limpias sin costo de runtime adicional.
- **Referencia:** [CMakeLists / pico_sdk_init en "Getting started"](https://www.raspberrypi.com/documentation/pico-sdk/index_doxygen.html)

---

## 2. Mapa de pines (GPIO)

- **Decisión:** Se respeta la asignación de Idea.md §4:

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
  oculto (Marsaglia, "Xorshift RNGs", 2003). La propia Idea.md acepta
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

## 7. Wi-Fi: `cyw43_arch` en modo *threadsafe background*

- **Decisión:** Se enlaza `pico_cyw43_arch_lwip_threadsafe_background`, que permite
  usar el driver CYW43 y lwIP desde **ambos núcleos** con servicio en segundo
  plano automático.
- **Conectividad:** STA (10 s de timeout) → si falla, SoftAP
  (`PicoW-ReactionGame`, IP `192.168.4.1`). El LED onboard queda fijo en STA y
  parpadea en AP.
- **Referencia:**
  - [pico_cyw43_arch](https://www.raspberrypi.com/documentation/pico-sdk/networking.html#pico_cyw43_arch)
  - `cyw43_arch_wifi_connect_timeout_ms`, `cyw43_arch_enable_ap_mode` en
    `pico/cyw43_arch.h`

---

## 8. Servidor web: lwIP (API raw) + NO_SYS=1

- **Decisión:** Servidor HTTP con la **API raw de lwIP** (`tcp_new`, `tcp_bind`,
  `tcp_listen`, `tcp_accept`, `tcp_recv`, `tcp_write`, `tcp_output`) en modo
  `NO_SYS=1` (sin sistema operativo).
- **Por qué:** Es el enfoque de los ejemplos oficiales `pico_w` y evita depender
  de un RTOS. Las callbacks corren en contexto de IRQ de baja prioridad que
  `pico_cyw43_arch_lwip_threadsafe_background` atiende automáticamente.
- **Configuración lwIP:** `lwipopts.h` con `MEM_SIZE=16000`, `PBUF_POOL_SIZE=32`
  y `TCP_SND_BUF=(8*TCP_MSS)` para acomodar la página HTML (~2 KB).
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
  del DHCP que el CYW43 ya provee en modo AP.
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
  formulario y su ruta propios (`/`, `/seed`, `/game`, `/display`, `/wifi`,
  `/player`).
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
