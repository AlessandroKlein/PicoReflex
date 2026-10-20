# Proyecto: Juego de Reacción Rápida Avanzado (Reaction Game)

**Materia:** Microprocesadores  
**Institución:** Universidad Nacional del Litoral - Tecnicatura en Automatización y Robótica  
**Plataforma:** Raspberry Pi Pico W  
**Entorno de desarrollo:** Raspberry Pi Pico C/C++ SDK  

---

## 1. Descripción del Proyecto

El sistema es un dispositivo de entrenamiento de reflejos basado en 5 LEDs y 5 pulsadores. El usuario debe presionar el botón correspondiente al LED que se enciende en el menor tiempo posible. 

El proyecto incluye control por **semillas pseudoaleatorias deterministas**, un **servidor web embebido para configuración**, **medición de batería con estimación de autonomía** y **conectividad dual (Wi-Fi STA / SoftAP)**.

---

## 2. Requerimientos Funcionales y Características Principales

### A. Generación Determinista por Semilla (PRNG Seed)
* **Motor PRNG:** Implementado con el generador de números pseudoaleatorios del SDK (`pico_rand` o generador xorshift/LCG en C++).
* **Comportamiento:** Si el usuario ingresa una semilla específica (ej. `SEED = 12345`), la secuencia de encendido de LEDs será **idéntica e irrepetible** en cada reinicio, ideal para competencias estandarizadas y pruebas repetibles.
* **Modo Aleatorio:** Si no se define semilla, el sistema utiliza el ADC no conectado (`adc_read()`) o el hardware de ruido de la Pico W como semilla inicial.

### B. Servidor Web Embebido (Configuración y Control)
* **Página Web Interna:** Alojada directamente en la memoria Flash de la Pico W usando la pila de red `lwIP` y el driver Wi-Fi `cyw43_arch`.
* **Parámetros Configurables en Tiempo Real:**
  * **Semilla (Seed):** Valor numérico para fijar la secuencia.
  * **Dificultad:** Tolerancia máxima de tiempo para presionar el botón (ej. Fácil: 1000ms, Normal: 500ms, Difícil: 250ms).
  * **Velocidad / Tiempo entre estímulos:** Intervalo de tiempo aleatorio/fijo entre apagado de un LED y encendido del siguiente.
  * **Modo de juego:** Entrenamiento libre, contrarreloj o muerte súbita.
* **Panel de Estadísticas Web:** Muestra tiempo promedio de reacción, mejor tiempo y tasa de aciertos/fallos.

### C. Conectividad Redundante (Modo Wi-Fi STA / Access Point)
* **Conexión Automática (Modo Station - STA):** Intenta conectarse a la red Wi-Fi configurada en memoria.
* **Conexión Contingente (Modo Access Point - SoftAP):** Si no hay red Wi-Fi disponible en 10 segundos, la Pico W inicia un Punto de Acceso propio (`PicoW-ReactionGame`). El usuario se conecta a esta red para ingresar a la IP por defecto `192.168.4.1`.

### D. Sistema de Monitoreo de Batería y Cálculo de Consumo
* **Lectura de Voltaje:** Utiliza el ADC interno de la Pico W (`ADC3` / `GPIO29` o pin VSYS) mediante un divisor resistivo para medir el voltaje real de la batería.
* **Cálculo de Porcentaje:** Conversión del voltaje medido según la curva de descarga de la batería seleccionada.
* **Especificaciones de Alimentación:**
  * **Batería recomendada:** Batería LiPo / Li-Ion de **1 celda (1S - 3.7V nominal, 4.2V carga completa)** de **1200 mAh** a **2000 mAh**, conectada a un módulo de carga y protección TP4056 + Step-Up/Boost regulado a 5V (o alimentando directamente a VSYS entre 1.8V y 5.5V).
  * **Consumo estimado del sistema:**
    * Pico W con Wi-Fi activo: ~70 mA - 90 mA.
    * 5 LEDs + Display OLED/7 Segments + Buzzer: ~30 mA - 50 mA.
    * **Consumo total máximo:** ~120 mA - 140 mA.
  * **Autonomía estimada:** Con una batería de **1800 mAh**, el sistema funcionará de forma continua aproximadamente **12 a 15 horas**.
* **Alerta en pantalla/web:** Muestra porcentaje de carga restante y advertencia de batería baja (< 15%).

---

## 3. Características Adicionales con Pico W SDK (C/C++)

Para enriquecer la complejidad técnica requerida por la materia, se incorporan las siguientes funcionalidades nativas del SDK:

1. **Almacenamiento No Volátil (EEPROM/Flash Simulation):**
   * Guardado de récords (Rankings), nombres de usuarios y la última semilla utilizada en la memoria Flash interna de la Pico W (`hardware_flash`), persistiendo aun después de apagar el equipo.
2. **Uso de Multinúcleo (Dual-Core Processing - `pico_multicore`):**
   * **Core 0:** Dedicado exclusivamente a la lógica del juego en tiempo real, captura precisa de interrupciones de los botones (`gpio_set_irq_enabled_with_callback`) y generación de tono mediante PWM (`hardware_pwm`).
   * **Core 1:** Asignado exclusivamente a la pila Wi-Fi (`cyw43_arch`), procesamiento de peticiones HTTP del servidor web y actualización del display.
3. **Mapeo de Botones por DMA (Direct Memory Access):**
   * Registro ultrasensible del timestamp de pulsación de botones mediante hardware DMA y temporizadores de alta precisión (`hardware_timer`), logrando una resolución de microsegundos ($\mu s$) en la medición de la reacción.
4. **Modos de Bajo Consumo (Sleep Modes):**
   * Si no se detecta actividad en 3 minutos, la Pico W entra en modo *Sleep*, desactivando el radio Wi-Fi y reduciendo el consumo a $< 2\text{ mA}$ hasta que se presione un botón de reinicio.
5. **Feedback Auditivo Polifónico / Melodías PWM:**
   * Buzzer pasivo controlado por PWM que reproduce tonos distintivos para respuestas correctas, fallos, cuenta regresiva y melodía de *Game Over*.

---

## 4. Esquema de Hardware y Periféricos

| Periférico | Pin GPIO (Pico W) | Función |
| :--- | :--- | :--- |
| **LEDs (x5)** | GPIO 2, 3, 4, 5, 6 | Indicadores luminosos de reacción |
| **Pulsadores (x5)** | GPIO 7, 8, 9, 10, 11 | Botones de entrada (configurados con Pull-Up) |
| **Display I2C (OLED SSD1306 / LCD 1602)** | SDA: GPIO 14 / SCL: GPIO 15 | Muestra tiempos, semilla, modo y batería |
| **Buzzer Pasivo** | GPIO 16 (PWM) | Feedback sonoro y alarmas |
| **Sensor Voltaje Batería (VSYS/ADC)** | GPIO 29 (ADC3) | Medición de nivel de batería |
| **LED Onboard CYW43** | Pin Wi-Fi | Estado de conexión (Blink = AP / Fijo = STA) |

---

## 5. Estructura de Software en C++ (`pico-sdk`)

```text
.
├── CMakeLists.txt          # Configuración del build system con Pico SDK
├── include/
│   ├── battery_monitor.hpp # Módulo de medición de voltaje y porcentaje
│   ├── game_logic.hpp      # Lógica del juego, temporizadores y PRNG por semilla
│   ├── web_server.hpp      # Servidor HTTP en lwIP (STA/SoftAP)
│   └── display_driver.hpp  # Manejo de pantalla I2C
└── src/
    ├── battery_monitor.cpp
    ├── game_logic.cpp
    ├── web_server.cpp
    └── main.cpp            # Inicialización de cores, Wi-Fi y loop principal
```

---

## 6. Puntos a Cubrir en el Informe Final (Según Consigna)

1. **Límites conceptuales:** Explicación del circuito de 5 LEDs, 5 botones, display I2C, buzzer PWM, lógica de semillas y división de tareas por multinúcleo.
2. **Descripción de la solución:** Diagrama circuital, código en C/C++ estructurado, configuración de CMake y capturas de la interfaz web de administración.
3. **Análisis de Periférico:** Detalle técnico del módulo ADC (para medición de batería) o del módulo PWM/I2C del microcontrolador RP2040.