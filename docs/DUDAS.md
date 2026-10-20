# Dudas y Consultas — Proyecto PicoReflex

> Archivo de dudas/consultas técnicas detectadas durante el desarrollo.
> Se actualiza a medida que se resuelven o se confirman con la cátedra / datasheet.

---

## 1. Medición de batería (VSYS) y conflicto con el Wi-Fi (GPIO29 / ADC3)

- **Duda:** En la Raspberry Pi Pico W, el pin `GPIO29` (ADC3) se usa **tanto** para
  medir `VSYS/3` (divisor resistivo interno 3:1) **como** para el reloj SPI
  (`WL_CLK`) del chip inalámbrico CYW43439.
- **Resolución (confirmada en el SDK):** El header de la placa
  [`boards/pico_w.h`](https://github.com/raspberrypi/pico-sdk/blob/master/src/boards/include/boards/pico_w.h)
  define `CYW43_USES_VSYS_PIN 1` y `PICO_VSYS_PIN 29`, y el propio comentario indica que,
  para usar el GPIO de VSYS, hay que **garantizar que el CYW43 no lo esté usando**,
  envolviendo la lectura entre `cyw43_thread_enter()` / `cyw43_thread_exit()`
  (equivalentes a `cyw43_arch_lwip_begin()` / `cyw43_arch_lwip_end()`).
- **Impacto en el código:** la lectura del ADC de batería se hace con esa protección,
  y se documenta como decisión (ver `battery_monitor.cpp`).
- **Pendiente de confirmación con la cátedra:** si la lectura del ADC debe hacerse
  con el Wi-Fi en pausa (bloqueando brevemente la radio) o si se acepta un muestreo
  con la radio inactiva.

## 2. Motor PRNG determinista (semilla fija)

- **Duda:** `pico_rand` (`pico/rand.h`) **no** expone una función para fijar la semilla
  de forma explícita; se auto-siembra con fuentes de entropía (ROSC, timer, RAM hash…).
- **Resolución:** para el modo *determinista* (semilla fija que produce la misma
  secuencia en cada reinicio) se implementa un **PRNG propio** (xorshift32) en
  `game_logic.cpp`. Para el *modo aleatorio* se usa `get_rand_32()` como semilla.
- **Pendiente:** confirmar si la cátedra exige específicamente `pico_rand` o acepta un
  generador software (la consigna dice "pico_rand o xorshift/LCG en C++", así que es válido).

## 3. Modo de bajo consumo (< 2 mA) en Pico W

- **Duda:** La consigna pide "Sleep < 2 mA". Según
  [`pico/low_power.h`](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_low_power),
  el modo *sleep* consume ~7.3 mA (Pico, VSYS), y el modo *dormant* ~0.95 mA.
  Para cumplir "< 2 mA" es necesario usar **dormant**, no *sleep*.
- **Resolución:** se usa `low_power_dormant_until_gpio_pin_state()` (wake por GPIO) con
  `DORMANT_CLOCK_SOURCE_ROSC`, tras `cyw43_arch_deinit()` para apagar la radio.
- **Pendiente de verificación en hardware:** comportamiento de la re-inicialización del
  CYW43 después de dormant, y si hace falta fuente de reloj externa para el RTC en RP2040
  (el propio header lo advierte).

## 4. Display OLED SSD1306 vs LCD1602

- **Duda:** La consigna menciona "Display I2C (OLED SSD1306 / LCD 1602)". Se elige
  **SSD1306 (128x64)** porque permite mostrar más información (tiempo, semilla, modo,
  batería) y usa I2C1 en GPIO14/15.
- **Pendiente:** confirmar el modelo exacto del display del alumno (¿SSD1306 o SH1106?
  ¿dirección I2C 0x3C o 0x3D?). El driver incluye ambas direcciones configurables.

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

## 8. Autonomía / consumo

- **Duda:** La consigna estima 12–15 h con 1800 mAh. Verificar con medición real.
- **Pendiente:** medir consumo real (Wi-Fi activo + LEDs + display) y ajustar la
  constante de autonomía en `battery_monitor`.

---

*Última actualización: ver `git log`.*
