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
- **Servidor web embebido** (lwIP) que permite configurar semilla, dificultad,
  modo, velocidad entre estímulos, duración de ronda y nombre, además de ver
  estadísticas (promedio, mejor tiempo, tasa de acierto) y el nivel de batería.
- **Conectividad redundante:** Station (STA) con *timeout* de 10 s y respaldo en
  Access Point (`PicoW-ReactionGame`, IP `192.168.4.1`).
- **Medición de batería** por ADC (VSYS/3) con estimación de porcentaje y alerta
  de batería baja (< 15 %).
- **Almacenamiento no volátil** en Flash (récords/ranking, nombre y última
  semilla).
- **Doble núcleo:** Core 0 (juego) y Core 1 (red + display).
- **Display OLED SSD1306** (I2C1) para mostrar tiempo, semilla, modo, batería y
  dirección IP.

**Límites / fuera de alcance:** no se implementa control de sonido polifónico
completo (solo tonos PWM), ni *datalogging* a nube, ni carga de batería (se asume
alimentación externa o módulo TP4056 + boost). El modo de ultra-bajo consumo
*dormant* (< 1 mA) queda documentado como mejora pendiente de verificación en
hardware (ver `docs/DUDAS.md`).

### b. Descripción de la solución propuesta

#### Circuito

- LEDs ánodo a GPIO 2–6 (resistencia limitadora 220–470 Ω), cátodo a GND.
- Pulsadores entre GPIO 7–11 y GND (pull-up interno de la RP2040).
- Display SSD1306 a I2C1 (SDA=GPIO14, SCL=GPIO15) con pull-ups de 4.7 kΩ.
- Buzzer pasivo a GPIO16 (PWM).
- Medición de batería en GPIO29 (ADC3), que en la Pico W mide VSYS/3.
- LED onboard controlado por el CYW43 (estado de red).

#### Software

El código está organizado en módulos (`include/` + `src/`), compilados con CMake
y el Pico SDK. Los puntos clave son:

- `main.cpp` arranca el Core 1 con `multicore_launch_core1()`.
- `game_logic.cpp` (Core 0) implementa el PRNG, la máquina de estados del juego y
  la temporización con IRQ + `time_us_32()`; usa DMA (buffer en anillo rítmado
  por PWM) como *trace* hardware de los pulsadores.
- `web_server.cpp` (Core 1) gestiona `cyw43_arch` (STA/AP) y sirve HTTP con la
  API raw de lwIP.
- `battery_monitor.cpp` lee el ADC protegiendo la lectura con
  `cyw43_arch_lwip_begin/end()` (GPIO29 compartido con el SPI del Wi-Fi).
- `flash_storage.cpp` persiste récords con `flash_safe_execute()`.
- `display_driver.cpp` dibuja texto 5×7 en el SSD1306.
- `audio.cpp` genera tonos PWM.
- `shared_state.cpp` sincroniza configuración/estadísticas entre núcleos con
  `critical_section` + `queue`.

Cada decisión está documentada y vinculada a la documentación oficial en
`docs/DECISIONES.md`.

### c. Conclusión

El proyecto integra una cantidad significativa de periféricos y APIs del RP2040
(GPIO, interrupciones, temporizadores, DMA, PWM, I2C, ADC, Flash, multinúcleo,
Wi-Fi/lwIP) en un dispositivo funcional y reproducible.

**Dificultades encontradas:**

1. **GPIO29 compartido** entre el ADC (VSYS/3) y el SPI del CYW43 en la Pico W:
   obligó a proteger cada lectura del ADC con el *lock* del CYW43, y a mover la
   medición de batería al Core 1 (que es quien inicializa el Wi-Fi).
2. **PRNG determinista:** `pico_rand` no permite fijar semilla, por lo que se
   implementó un `xorshift32` propio para el modo determinista.
3. **Escritura segura en Flash con dos núcleos:** se resolvió con
   `flash_safe_execute()` y `flash_safe_execute_core_init()` en ambos núcleos.
4. **Bajo consumo real (< 2 mA):** requiere modo *dormant* (no *sleep*), que exige
   apagar el Wi-Fi y coordinar ambos núcleos; quedó documentado como mejora
   pendiente de verificación en hardware.
5. **Comunicación inter-núcleo:** se evitó el uso de los FIFOs crudos y se usó
   `queue_t` + `critical_section`, siguiendo la recomendación del SDK.

---

## 2. Análisis de Periférico: ADC (medición de batería)

### Funcionamiento

La RP2040 dispone de un **ADC SAR** (aproximaciones sucesivas) de **12 bits**
(0–4095) con 5 entradas multiplexadas: 4 en los pines GPIO 26–29 (ADC0–ADC3) y
una interna para el sensor de temperatura. Su tensión de referencia es de 3.3 V.

En la **Pico W**, el pin **GPIO29 (ADC3)** está conectado a **VSYS a través de un
divisor resistivo interno 3:1**, de modo que la tensión medida en el pin es
`VSYS/3`. Esto permite monitorizar una batería LiPo/Li-Ion de 1 celda
(3.0–4.2 V) conectada a VSYS.

### Configuración (SDK)

- `adc_init()`: habilita el ADC y configura su reloj (~500 kS/s con reloj
  independiente de 48 MHz).
- `adc_select_input(3)`: selecciona la entrada ADC3 (GPIO29).
- `adc_read()`: inicia una conversión y devuelve el valor (0–4095).
- **Particularidad Pico W:** GPIO29 también es `WL_CLK` (SPI del CYW43439). Para
  no chocar con la radio, la lectura se envuelve entre `cyw43_arch_lwip_begin()` y
  `cyw43_arch_lwip_end()` (ver `battery_monitor.cpp`).

### Conversión tensión → porcentaje

```
Vpin = adc · 3300 / 4095        (mV)
VSYS = 3 · Vpin                 (divisor 3:1)
```

El porcentaje se obtiene por interpolación lineal de la curva de descarga de una
celda LiPo: 4200 mV → 100 %, 3700 mV → 50 %, 3000 mV → 0 %. Se promedian 16
muestras para reducir ruido. Se genera alerta por batería baja (< 15 %).

### Características relevantes

- Resolución: 12 bits (1 LSB ≈ 0.8 mV; sobre VSYS ≈ 2.4 mV por LSB tras el ×3).
- Entradas compartidas con GPIO; en Pico W, ADC3 compartido con el Wi-Fi.
- Posibilidad de muestreo por *round-robin* y DMA (no usado en la medición, que
  es por *polling* protegido).

---

## 3. Exposición

Para la exposición se recomienda demostrar: (1) una ronda con semilla fija y
repetirla mostrando la misma secuencia; (2) el panel web en modo AP; (3) la
medición de batería y su alerta de batería baja; (4) la división de tareas entre
núcleos.
