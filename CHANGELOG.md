# Changelog

Todas las versiones notables de PicoReflex se documentan en este archivo.
El formato sigue [Keep a Changelog](https://keepachangelog.com/) y el versionado
sigue [Semantic Versioning](https://semver.org/) (SemVer).

## [0.1.0] - 2026-10-20

### Añadido
- **Motor de juego de reacción** (5 LEDs / 5 pulsadores) con temporización en
  microsegundos (IRQ GPIO + `time_us_32`).
- **PRNG determinista por semilla** (`xorshift32`) + modo aleatorio (`pico_rand`).
- **Servidor web embebido** (lwIP) con modo Station (STA) y fallback a SoftAP.
- **Captive portal**: servidor DNS (puerto 53) que redirige a `192.168.4.1`.
- **Medición de batería** por ADC (VSYS/3) con estimación de porcentaje y alerta
  de batería baja (< 15 %).
- **Modo DORMANT** (< 2 mA) con protocolo dual-core (apagado de Wi-Fi + wake por
  pulsador).
- **Almacenamiento no volátil en Flash** (ranking, nombre y semilla) con
  `flash_safe_execute`.
- **Display OLED SSD1306** (I2C1) con auto-detección de dirección (0x3C/0x3D) y
  fuente 5x7.
- **Buzzer PWM** (tonos de acierto, fallo, timeout, cuenta regresiva y game over).
- **Documentación técnica** en `docs/`: DECISIONES, ARQUITECTURA, INFORME,
  DUDAS y COMPILAR.

### Estructura de código
- `include/` + `src/`: `main`, `shared_state`, `game_logic`, `audio`,
  `display_driver`, `battery_monitor`, `flash_storage`, `web_server`,
  `captive_portal`.
- `CMakeLists.txt` (Pico SDK 2.3.0, board `pico_w`), `lwipopts.h`.
- `tools/gen_font.py` (generador de la fuente 5x7).
