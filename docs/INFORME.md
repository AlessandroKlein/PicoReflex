# Informe Técnico — PicoReflex

**Materia:** Microprocesadores
**Institución:** Universidad Nacional del Litoral — Tecnicatura en Automatización y Robótica
**Plataforma:** Raspberry Pi Pico W — Raspberry Pi Pico C/C++ SDK

---

## 1. Informe Técnico

### a. Límites conceptuales del circuito y la lógica

El alcance del proyecto es un **dispositivo de entrenamiento de reflejos** con
las siguientes características concretas:

- **5 LEDs indicadores** (GPIO 2–6) y **5 pulsadores** (GPIO 7–11, con pull-up
  interno y detección por flanco de bajada). Cada LED tiene un botón asociado
  uno a uno.
- **Medición del tiempo de reacción** con resolución de **microsegundos**
  (`time_us_32()` capturado en la interrupción GPIO).
- **Generación determinista por semilla:** con una semilla fija, la secuencia de
  LEDs es idéntica en cada reinicio (PRNG `xorshift32`); sin semilla, se siembra
  con `get_rand_32()`.
- **Tres modos de juego:** entrenamiento libre (sin límite de tiempo),
  contrarreloj (ronda de duración fija) y muerte súbita (un fallo termina la
  ronda). Tres dificultades (1000 / 500 / 250 ms de tolerancia).
- **Feedback sonoro** mediante un buzzer pasivo (PWM): tono de acierto, de fallo,
  de timeout, cuenta regresiva y melodía de *game over*.
- **Servidor web embebido** (lwIP) con **páginas separadas**: Home (red, batería,
  estadísticas y tabla de tiempos), semilla (`/seed`), juego (`/game`), display
  (`/display`), Wi-Fi (`/wifi`) y jugador (`/player`). El frontend (HTML) está
  separado del backend (ruteo/parseo).
- **Disparador de inicio con dos botones simultáneos** y **cuenta regresiva**
  configurable (`start_delay_s`, por defecto 3 s, límite 10 s) antes de arrancar.
- **Wi-Fi configurable desde la web**: SSID/password se editan en `/wifi`, se
  persisten en Flash y se re-conecta automáticamente (sin recompilar).
- **OLED deshabilitable** desde la web (`/display`) para ahorrar energía; toda la
  información sigue disponible en la web.
- **Historial de tiempos de reacción** (últimos 20) mostrado como tabla en el Home.
- **Conectividad redundante:** Station (STA) con *timeout* de 10 s y respaldo en
  Access Point (`PicoW-ReactionGame`, IP `192.168.4.1`) con **captive portal**
  (servidor DNS en puerto 53 que redirige toda consulta a `192.168.4.1`).
- **Medición de batería** por ADC con divisor resistivo externo (10 kΩ/22 kΩ) en
  GPIO26, con estimación de porcentaje y alerta de batería baja (< 15 %).
- **Almacenamiento no volátil** en Flash (récords/ranking, nombre y última
  semilla).
- **Doble núcleo:** Core 0 (juego) y Core 1 (red + display).
- **Display OLED SSD1306** (I2C1) para mostrar tiempo, semilla, modo, batería y
  dirección IP.

**Límites / fuera de alcance:** no se implementa control de sonido polifónico
completo (solo tonos PWM), ni *datalogging* a nube, ni carga de batería (se asume
alimentación externa o módulo TP4056 + boost). El modo de ultra-bajo consumo
*DORMANT* (< 1.5 mA) está **implementado** con un protocolo dual-core (apagado
del Wi-Fi + `low_power_dormant_until_gpio_pin_state`); queda pendiente su
verificación de consumo en hardware (ver `docs/DECISIONES.md` §13).

### b. Descripción de la solución propuesta

#### Circuito

- LEDs ánodo a GPIO 2–6 (resistencia limitadora 220–470 Ω), cátodo a GND.
- Pulsadores entre GPIO 7–11 y GND (pull-up interno de la RP2040).
- Display SSD1306 a I2C1 (SDA=GPIO14, SCL=GPIO15) con pull-ups de 4.7 kΩ.
- Buzzer pasivo a GPIO16 (PWM).
- Medición de batería en GPIO26 (ADC0) mediante divisor externo 10 kΩ/22 kΩ
  (R1 entre batería y pin, R2 entre pin y GND).
- LED onboard controlado por el CYW43 (estado de red).

#### Software

El código está organizado en módulos (`include/` + `src/`), compilados con CMake
y el Pico SDK. Los puntos clave son:

- `main.cpp` arranca el Core 1 con `multicore_launch_core1()`.
- `game_logic.cpp` (Core 0) implementa el PRNG, la máquina de estados del juego,
  el **disparador de dos botones + cuenta regresiva** y la temporización con IRQ +
  `time_us_32()`; usa DMA (buffer en anillo rítmado por PWM) como *trace* hardware
  de los pulsadores.
- `web_server.cpp` (Core 1) gestiona `cyw43_arch` (STA/AP, reconexión) y es el
  **backend** HTTP (ruteo/parseo) con la API raw de lwIP; `web_ui.cpp` es el
  **frontend** que genera las páginas HTML (`web_ui::render`).
- `battery_monitor.cpp` lee el ADC (GPIO26/ADC0) y aplica el factor del divisor
  externo `×32/22`.
- `flash_storage.cpp` persiste récords y configuración (Wi-Fi, OLED, cuenta
  regresiva) con `flash_safe_execute()` (estructura `PersistentData` v2).
- `display_driver.cpp` dibuja texto 5×7 en el SSD1306 y permite apagarlo
  (`set_power`).
- `audio.cpp` genera tonos PWM.
- `shared_state.cpp` sincroniza configuración/estadísticas/historial entre núcleos
  con `critical_section` + `queue`.

Cada decisión está documentada y vinculada a la documentación oficial en
`docs/DECISIONES.md`.

### c. Conclusión

El proyecto integra una cantidad significativa de periféricos y APIs del RP2040
(GPIO, interrupciones, temporizadores, DMA, PWM, I2C, ADC, Flash, multinúcleo,
Wi-Fi/lwIP) en un dispositivo funcional y reproducible.

**Dificultades encontradas:**

1. **GPIO29 ocupado en la Pico W** por el SPI del Wi-Fi y el divisor interno VSYS/3:
   se resolvió midiendo la batería con un divisor externo en GPIO26 (ADC0), que
   evita el conflicto por completo.
2. **PRNG determinista:** `pico_rand` no permite fijar semilla, por lo que se
   implementó un `xorshift32` propio para el modo determinista.
3. **Escritura segura en Flash con dos núcleos:** se resolvió con
   `flash_safe_execute()` y `flash_safe_execute_core_init()` en ambos núcleos.
4. **Bajo consumo real (< 2 mA):** requiere modo *dormant* (no *sleep*), que exige
   apagar el Wi-Fi (`cyw43_arch_deinit()`) y coordinar ambos núcleos; se implementó
   con un protocolo de *handshake* y `low_power_dormant_until_gpio_pin_state()`,
   pendiente de verificación de consumo en hardware.
5. **Comunicación inter-núcleo:** se evitó el uso de los FIFOs crudos y se usó
   `queue_t` + `critical_section`, siguiendo la recomendación del SDK.

---

## 2. Análisis de Periférico: ADC (medición de batería)

### Funcionamiento

La RP2040 dispone de un **ADC SAR** (aproximaciones sucesivas) de **12 bits**
(0–4095) con 5 entradas multiplexadas: 4 en los pines GPIO 26–29 (ADC0–ADC3) y
una interna para el sensor de temperatura. Su tensión de referencia es de 3.3 V.

En la **Pico W**, la batería se mide con un **divisor resistivo externo** en
**GPIO26 (ADC0)**: `R1 = 10 kΩ` (batería → pin ADC) y `R2 = 22 kΩ` (pin ADC → GND).
Se eligió GPIO26 porque está libre; GPIO29 (ADC3) está ocupado por el SPI del
Wi-Fi y por el divisor interno VSYS/3.

### Configuración (SDK)

- `adc_init()`: habilita el ADC y configura su reloj (~500 kS/s con reloj
  independiente de 48 MHz).
- `adc_gpio_init(26)`: configura GPIO26 como entrada analógica (función NULL,
  pulls y receptor digital deshabilitados).
- `adc_select_input(0)`: selecciona la entrada ADC0 (GPIO26).
- `adc_read()`: inicia una conversión y devuelve el valor (0–4095).

### Conversión tensión → porcentaje

```
Vpin = adc · 3300 / 4095              (mV)
Vbat = Vpin · (R1 + R2) / R2
     = Vpin · 32 / 22                 (divisor 10k/22k)
```

Con una batería a 4.2 V, el pin recibe `4.2 · 22/32 ≈ 2.89 V`, por debajo de la
referencia de 3.3 V (sin recorte). El porcentaje se obtiene por interpolación
lineal de la curva de descarga de una celda LiPo: 4200 mV → 100 %, 3700 mV →
50 %, 3000 mV → 0 %. Se promedian 16 muestras para reducir ruido. Se genera alerta
por batería baja (< 15 %).

### Características relevantes

- Resolución: 12 bits (1 LSB ≈ 0.8 mV en el pin; ≈ 1.17 mV sobre Vbat tras ×32/22).
- Entradas compartidas con GPIO; se usa ADC0 (GPIO26), libre en la Pico W.
- Posibilidad de muestreo por *round-robin* y DMA (no usado en la medición, que
  es por *polling* simple).

---

## 3. Exposición

Para la exposición se recomienda demostrar: (1) una ronda con semilla fija y
repetirla mostrando la misma secuencia; (2) el panel web en modo AP; (3) la
medición de batería y su alerta de batería baja; (4) la división de tareas entre
núcleos.
