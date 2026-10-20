# Dudas y Consultas — Proyecto PicoReflex

> Archivo de dudas/consultas técnicas detectadas durante el desarrollo.
> Se actualiza a medida que se resuelven o se confirman con la cátedra / datasheet.
>
> **Estado tras la revisión de cátedra:** los puntos §1, §2, §4, §5, §6 y §8 quedan
> validados; §3 (DORMANT) y §4 (escáner I2C) fueron implementados en el código.
> Ver `git log` para el historial.

---

## 1. Medición de batería — ✅ RESUELTO (divisor externo)

- **Duda inicial:** en la Pico W, `GPIO29` (ADC3) mide `VSYS/3` (divisor interno
  3:1) y además es el reloj SPI (`WL_CLK`) del CYW43439, por lo que había conflicto.
- **Resolución final:** se descartó GPIO29 y se usa un **divisor resistivo externo**
  en **GPIO26 (ADC0)** — libre en la Pico W — con `R1 = 10 kΩ` (batería → pin ADC)
  y `R2 = 22 kΩ` (pin ADC → GND). Conversión: `Vbat = Vpin · (R1+R2)/R2 = Vpin · 32/22`.
  Sin conflicto con el Wi-Fi y sin necesidad del lock del CYW43.
- **Verificación en hardware pendiente:** confirmar el pin exacto y calibrar el
  porcentaje con una medición real.

## 2. Motor PRNG determinista (semilla fija)

- **Duda:** `pico_rand` (`pico/rand.h`) **no** expone una función para fijar la semilla
  de forma explícita; se auto-siembra con fuentes de entropía (ROSC, timer, RAM hash…).
- **Resolución:** para el modo *determinista* (semilla fija que produce la misma
  secuencia en cada reinicio) se implementa un **PRNG propio** (xorshift32) en
  `game_logic.cpp`. Para el *modo aleatorio* se usa `get_rand_32()` como semilla.
- **Pendiente:** confirmar si la cátedra exige específicamente `pico_rand` o acepta un
  generador software (la consigna dice "pico_rand o xorshift/LCG en C++", así que es válido).

## 3. Modo de bajo consumo (< 2 mA) en Pico W — ✅ IMPLEMENTADO

- **Duda:** La consigna pide "Sleep < 2 mA". Según
  [`pico/low_power.h`](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_low_power),
  el modo *sleep* consume ~7.3 mA (Pico, VSYS), y el modo *dormant* ~0.95 mA.
  Para cumplir "< 2 mA" es necesario usar **dormant**, no *sleep*.
- **Resolución (implementada):** protocolo dual-core en `game_logic.cpp` /
  `web_server.cpp`:
  1. Core 0 activa `g_sleep_request` tras 3 min de inactividad.
  2. Core 1 ejecuta `cyw43_arch_deinit()` (apaga la radio, 20–40 mA de ahorro) y
     responde `g_sleep_ready`.
  3. Core 0 habilita el wake-up GPIO en los 5 pulsadores y llama
     `low_power_dormant_until_gpio_pin_state(DORMANT_CLOCK_SOURCE_ROSC)`.
  4. Al despertar, el SDK restaura los relojes; Core 1 re-inicializa con
     `cyw43_arch_init()`.
- **Pendiente de verificación en hardware:** ciclo `cyw43_arch_deinit()` →
  dormant → `cyw43_arch_init()` con el modo *threadsafe background*, y confirmar
  el consumo real (< 1.5 mA) con un multímetro.

## 4. Display OLED SSD1306 vs LCD1602 — ✅ AUTO-DETECCIÓN

- **Duda:** La consigna menciona "Display I2C (OLED SSD1306 / LCD 1602)". Se elige
  **SSD1306 (128x64)** porque permite mostrar más información (tiempo, semilla, modo,
  batería) y usa I2C1 en GPIO14/15.
- **Resolución (implementada):** `display_driver.cpp` incluye un **escáner I2C** en
  `init()` que prueba `0x3C` y `0x3D` y usa la que responde (ACK), evitando
  fallos de bus. Diferencia SSD1306 vs SH1106: el SH1106 tiene RAM 132×64 (2 px
  de desfase); el driver actual apunta a SSD1306.

## 5. Mapeo de botones por DMA

- **Duda:** "Registro ultrasensible del timestamp mediante DMA + hardware_timer".
  Técnicamente DMA **copia** datos (no "mide" tiempo por sí solo). El timestamp de
  microsegundos lo aporta el temporizador hardware (`time_us_32()`).
- **Resolución:** se mide la reacción con **interrupción GPIO + `time_us_32()`**
  (método más preciso y de menor latencia). Además se usa **DMA** para registrar
  un *log* de eventos de botones (máscara de GPIO + timestamp) con captura hardware,
  cumpliendo el requisito de uso de DMA de forma correcta y demostrable.
- **Pendiente:** confirmar con la cátedra que este enfoque (IRQ para timing + DMA para
  log) satisface el punto 3 de "Características Adicionales".

## 6. Dirección de memoria Flash para almacenamiento no volátil

- **Duda:** ¿Dónde escribir los récords sin pisar el firmware?
- **Resolución:** se reserva el **último sector de Flash** (en la Pico W son 2 MB →
  `0x1FF000`, alineado a 4096). Se documenta cómo adaptarlo al tamaño real de Flash.
- **Pendiente:** confirmar el tamaño de Flash de la placa del alumno (2 MB estándar).

## 7. Credenciales Wi-Fi

- **Duda:** ¿Cómo se configuran SSID/password de la red STA si el dispositivo es móvil?
- **Resolución:** valores por defecto en `config.hpp` (`WIFI_SSID` / `WIFI_PASSWORD`),
  configurables en tiempo de compilación y (a futuro) vía web. El modo AP de respaldo
  (`PicoW-ReactionGame`, IP `192.168.4.1`) permite configurarlo sin recompilar.

## 8. Autonomía / consumo — ✅ VALIDADO

- **Consolidado (cátedra):**

  | Estado | Consumo |
  |---|---|
  | Wi-Fi + CPU dual-core (125 MHz) | ~70–90 mA |
  | LEDs + OLED + buzzer | ~20–35 mA |
  | Total en juego | ~100–120 mA |
  | Dormant | < 1.5 mA |

- **Autonomía:** LiPo 1800 mAh con descarga útil 85% → `1800·0.85/110 ≈ 13.9 h`.
- **Pendiente:** medir consumo real con multímetro y ajustar la constante de
  autonomía en `battery_monitor`.

---

*Última actualización: ver `git log`.*
