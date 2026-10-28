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
             | game_logic  (motor)    |   estado     | web_server (backend) |
             | audio       (buzzer)   |  compartido  | web_ui     (frontend)|
             | flash_storage (récords)|<------------>| display_driver      |
             |   |   |   |            | critical_sect| battery_monitor     |
             |                        |   + queue    | cyw43_arch / lwIP   |
             |                        |              | captive_portal      |
             +---+---+---+------------+              +---------------------+
                 |   |   |                 |
       +---------+   |   +---------+       |
       v             v             v       v
    GPIO 2..6    GPIO 7..11    GPIO 16   GPIO 14/15
     (LEDs)     (pulsadores)   (buzzer)   (I2C1 SSD1306/SH1106)
                          \      /
                           ADC0 (GPIO26) -> divisor externo 10k/22k (batería)
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
│   ├── display_driver.hpp    # OLED SSD1306/SH1106
│   ├── battery_monitor.hpp   # ADC batería
│   ├── flash_storage.hpp     # Persistencia en Flash
│   ├── web_server.hpp        # Servidor HTTP + Wi-Fi (backend)
│   ├── web_ui.hpp            # Frontend: generación de las páginas HTML
│   ├── captive_portal.hpp    # Servidor DNS (captive portal, modo AP)
│   ├── dhcpserver.h          # Servidor DHCP del AP (MicroPython/pico-examples)
│   └── font5x7.hpp           # Fuente 5x7 (generada por tools/gen_font.py)
├── src/
│   ├── main.cpp              # Arranque de ambos núcleos
│   ├── shared_state.cpp      # critical_section + queues
│   ├── game_logic.cpp        # Motor (Core 0)
│   ├── audio.cpp
│   ├── display_driver.cpp
│   ├── battery_monitor.cpp
│   ├── flash_storage.cpp
│   ├── web_server.cpp        # HTTP + Wi-Fi + display (backend, Core 1)
│   ├── web_ui.cpp            # HTML de las páginas (frontend)
│   ├── captive_portal.cpp    # DNS 53 -> 192.168.4.1 (captive portal)
│   └── dhcpserver.c          # DHCP del AP: IPs 192.168.4.16..23
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
            | 2 botones simultáneos / START_ROUND (web)      |
            v                                               |
   +-------------------+  cuenta regresiva (start_delay_s)  |
   | CUENTA REGRESIVA  |  beeps "3-2-1"                     |
   +---------+---------+                                    |
             v                                              |
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
un *trace* hardware de los estados de los botones (ver DECISIONES.md §4–§5). El
inicio requiere **dos botones simultáneos** y pasa por una **cuenta regresiva**
configurable (`start_delay_s`, ver DECISIONES.md §16).

## 5. Flujo de red (Core 1)

```text
cyw43_arch_init()
      |
      +-- cyw43_arch_enable_sta_mode()
      |     cyw43_arch_wifi_connect_timeout_ms(10 s)
      |
      +-- ¿conectado?
            |-- SÍ -> STA (LED fijo) + DHCP -> IP
            |-- NO -> disable_sta + enable_ap_mode("PicoW-ReactionGame", WPA2)
            |         IP 192.168.4.1 (canal 1, LED parpadea)
            |         captive::init()  -> DNS puerto 53 -> 192.168.4.1
            |         ap_dhcp_start()  -> DHCP IPs 192.168.4.16..23
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
| `GET /` | Home: red, batería, stats y tabla de tiempos |
| `GET /seed?seed=..` | Semilla del PRNG |
| `GET /game?difficulty=..&mode=..&smin=..&smax=..&round=..&delay=..` | Dificultad/modo/velocidad/ronda/cuenta regresiva |
| `GET /display?oled=..` | Habilita/deshabilita el OLED |
| `GET /wifi?ssid=..&pass=..` | Credenciales Wi-Fi (persistidas + reconexión, "Mostrar contraseña") |
| `GET /start` | `control_send(START_ROUND)` (con cuenta regresiva) |
| `GET /stop` | `control_send(STOP_ROUND)` |
| `GET /reset` | `control_send(RESET_STATS)` |

> Eliminada `GET /player` (v0.4.0). La ruta `GET /scan` quedó reservada/interna
> (sin uso visible; ver `docs/API.md`).

> Ver `docs/API.md` para el detalle de parámetros y rangos.

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
| SSD1306/SH1106 SDA/SCL | 14/15 | I2C1, 400 kHz, dirección 0x3C/0x3D |
| Buzzer pasivo | 16 | PWM (onda cuadrada) |
| Batería | 26 (ADC0) | Divisor externo 10 kΩ/22 kΩ |
| LED onboard | CYW43 | Estado de red (fijo=STA, blink=AP) |

> La batería se mide con un divisor externo en GPIO26; ver DECISIONES.md §9.
>
> **Referencia:** [Pinout oficial Raspberry Pi Pico W (A4, RP-008315-DS-1)](https://pip-assets.raspberrypi.com/categories/686-raspberry-pi-pico-w/documents/RP-008315-DS-1-PicoW-A4-Pinout.pdf)

## 8. Estimación de consumo

| Estado | Consumo estimado |
|---|---|
| Wi-Fi activo (STA/AP) + CPU dual-core (125 MHz) | ~70–90 mA |
| 5 LEDs + OLED + buzzer (promedio) | ~20–35 mA |
| **Total en juego** | ~100–120 mA |
| **Dormant (sueño profundo)** | **< 1.5 mA** |

- Autonomía con LiPo 1S de 1800 mAh (descarga útil 85%): ~13.9 h
  (`1800 mAh · 0.85 / 110 mA`).
- El modo *idle* implementa **DORMANT** (no *sleep*): apaga el Wi-Fi y entra en
  `low_power_dormant_until_gpio_pin_state(DORMANT_CLOCK_SOURCE_ROSC)`, con
  consumo < 1.5 mA (ver DECISIONES.md §13).
