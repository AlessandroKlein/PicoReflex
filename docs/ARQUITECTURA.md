# Arquitectura del Proyecto — PicoReflex

## 1. Visión general

PicoReflex es un **dispositivo de entrenamiento de reflejos** sobre Raspberry Pi
Pico W (RP2040). Consta de 5 LEDs y 5 pulsadores: el usuario debe presionar el
botón que corresponde al LED encendido en el menor tiempo posible. El sistema
añade control por semilla determinista, servidor web embebido, medición de
batería, almacenamiento no volátil y doble núcleo.

## 2. Diagrama de bloques

```text
                        +--------------------------------------------+
                        |              Raspberry Pi Pico W            |
                        |                (RP2040 + CYW43439)          |
                        +--------------------------------------------+
                          |                                    |
             +------------+-----------+              +---------+-----------+
             |        Core 0 (juego)   |              |   Core 1 (red/UI)   |
             +------------------------+              +---------------------+
             | game_logic  (motor)    |   estado     | web_server (HTTP)   |
             | audio       (buzzer)   |  compartido  | display_driver      |
             | flash_storage (récords)|<------------>| battery_monitor     |
             |   |   |   |            | critical_sect| cyw43_arch / lwIP   |
             +---+---+---+------------+   + queue    +---------------------+
                 |   |   |                 |
       +---------+   |   +---------+       |
       v             v             v       v
    GPIO 2..6    GPIO 7..11    GPIO 16   GPIO 14/15
     (LEDs)     (pulsadores)   (buzzer)   (I2C1 SSD1306)
                          \      /
                           ADC3 (GPIO29) -> VSYS/3 (batería)
```

## 3. Estructura de archivos

```text
PicoReflex/
├── CMakeLists.txt            # Build system (Pico SDK)
├── lwipopts.h                # Configuración de lwIP
├── pico_sdk_import.cmake     # Import del SDK (VS Code extension)
├── include/
│   ├── pin_defs.hpp          # Mapa de pines
│   ├── config.hpp            # Config/estadísticas + estado compartido (API)
│   ├── game_logic.hpp        # Motor del juego + PRNG
│   ├── audio.hpp             # Buzzer PWM
│   ├── display_driver.hpp    # OLED SSD1306
│   ├── battery_monitor.hpp   # ADC batería
│   ├── flash_storage.hpp     # Persistencia en Flash
│   ├── web_server.hpp        # Servidor HTTP + Wi-Fi
│   └── font5x7.hpp           # Fuente 5x7 (generada por tools/gen_font.py)
├── src/
│   ├── main.cpp              # Arranque de ambos núcleos
│   ├── shared_state.cpp      # critical_section + queues
│   ├── game_logic.cpp        # Motor (Core 0)
│   ├── audio.cpp
│   ├── display_driver.cpp
│   ├── battery_monitor.cpp
│   ├── flash_storage.cpp
│   └── web_server.cpp        # HTTP + Wi-Fi + display (Core 1)
├── tools/
│   └── gen_font.py           # Generador de la fuente 5x7
└── docs/
    ├── DUDAS.md
    ├── DECISIONES.md
    ├── ARQUITECTURA.md
    └── INFORME.md
```

## 4. Flujo del juego (Core 0)

```text
        +--------+
        | IDLE   |<-----------------------------------------+
        +---+----+                                          |
            | pulsación / START_ROUND (web)                 |
            v                                               |
   +----------------+  presenta LED + t0=time_us_32()       |
   | ROUND (activa) |----------------------------------+    |
   +----------------+                                  |    |
        |            espera pulsación o timeout        |    |
        v                                             v    |
   +------------------------------+                   |    |
   | evalúa resultado             |                   |    |
   | hit / miss / timeout         |                   |    |
   +-------------+----------------+                   |    |
                 | stats_record + audio + flash       |    |
                 |                                    |    |
                 |   (SUDDEN_DEATH y fallo)           |    |
                 |   (TIMED y fin de tiempo)          +----+
                 |   (STOP_ROUND)
```

La medición de reacción usa la IRQ GPIO + `time_us_32()` (µs); el DMA mantiene
un *trace* hardware de los estados de los botones (ver DECISIONES.md §4–§5).

## 5. Flujo de red (Core 1)

```text
cyw43_arch_init()
      |
      +-- cyw43_arch_enable_sta_mode()
      |     cyw43_arch_wifi_connect_timeout_ms(10 s)
      |
      +-- ¿conectado?
            |-- SÍ -> STA (LED fijo) + DHCP -> IP
            |-- NO -> disable_sta + enable_ap_mode("PicoW-ReactionGame")
            |         IP 192.168.4.1 (LED parpadea)
      |
      v
http_server_init()  (lwIP raw API, puerto 80)
      |
      v
bucle: refresca display + muestrea batería (~1 Hz)
```

Rutas HTTP:

| Ruta | Efecto |
|---|---|
| `GET /` | Página de control + estadísticas |
| `GET /config?seed=..&difficulty=..&mode=..&smin=..&smax=..&round=..&name=..` | Aplica configuración |
| `GET /start` | `control_send(START_ROUND)` |
| `GET /stop` | `control_send(STOP_ROUND)` |
| `GET /reset` | `control_send(RESET_STATS)` |

## 6. Sincronización inter-núcleo

- `critical_section_t` (`pico/sync.h`): protege `GameConfig`, `GameStats` y
  `SystemStatus`. Accesos muy cortos (copiar/guardar struct).
- `queue_t` (`pico/util/queue.h`): cola de eventos de control Core1→Core0
  (`START_ROUND`, `STOP_ROUND`, `RESET_STATS`).
- `volatile bool g_round_active`: flag de estado de ronda (Core 0 escribe,
  Core 1 lee para el display).

## 7. Esquema de hardware (resumen)

| Periférico | Pin GPIO | Notas |
|---|---|---|
| LED 0..4 | 2..6 | Ánodo por GPIO (con resistencia ~220–470 Ω) |
| Botón 0..4 | 7..11 | Pull-up interno, cierra a GND al pulsar |
| SSD1306 SDA/SCL | 14/15 | I2C1, 400 kHz, dirección 0x3C |
| Buzzer pasivo | 16 | PWM (onda cuadrada) |
| Batería | 29 (ADC3) | VSYS/3, divisor 3:1 interno |
| LED onboard | CYW43 | Estado de red (fijo=STA, blink=AP) |

> La medición de VSYS en la Pico W comparte GPIO29 con el SPI del Wi-Fi; ver
> DECISIONES.md §9 y DUDAS.md §1.

## 8. Estimación de consumo

- Pico W + Wi-Fi activo: ~70–90 mA.
- 5 LEDs + display + buzzer: ~30–50 mA.
- Total pico: ~120–140 mA.
- Autonomía con LiPo 1800 mAh: ~12–15 h (estimado; ver DUDAS.md §8).
- Idle (WFI, Wi-Fi apagado): objetivo < 2 mA (pendiente de verificación con modo
  *dormant*).
