# Changelog

Todas las versiones notables de PicoReflex se documentan en este archivo.
El formato sigue [Keep a Changelog](https://keepachangelog.com/) y el versionado
sigue [Semantic Versioning](https://semver.org/) (SemVer).

## [0.1.3] - 2026-10-20

### Arreglado (seguridad)
- **XSS almacenado en el campo `name`** (`src/web_server.cpp`): el nombre del
  jugador se insertaba en el HTML sin escapar (`value='%s'`). Ahora se escapa con
  `html_escape()`. Ver `docs/SEGURIDAD.md`.
- **Wrap en `smin`/`smax`** (`src/web_server.cpp`): se acotan los valores a
  50..60000 ms y se garantiza `smin <= smax` (y `round` a 1..3600 s), evitando el
  *wrap* de `span = smax - smin + 1` en `game_logic.cpp`.

### Cambiado
- **CI unificado en `.github/workflows/ci.yml`**: compila en cada push/PR y,
  en tags `v*`, crea la release de GitHub con el firmware adjunto (reemplaza al
  anterior `build.yml`).

### Añadido
- `docs/ROADMAP.md`: hoja de ruta de mejoras (P0–P3) con referencias oficiales
  (reemplaza a `docs/MEJORAS.md`).

## [0.1.2] - 2026-10-20

### Añadido
- **Docker** (`Dockerfile` + `docker-compose.yml`): entorno de compilación
  reproducible (toolchain ARM, CMake, Ninja y Pico SDK 2.3.0 con submódulos).
- **CI** (`.github/workflows/ci.yml`): compila el firmware en cada push/PR y
  publica `build/*.uf2` como artefacto (provee el "Check status" para releases).

### Arreglado
- **`SECURITY.md`**: se reemplazó el contenido heredado de un proyecto web
  (`JWT_SECRET`, `CORS_ORIGIN`, `helmet`, superadmin) por prácticas de despliegue
  específicas de la Pico W (contraseña del AP, credenciales Wi-Fi, red local).

## [0.1.1] - 2026-10-20

### Arreglado
- **Desborde de búfer en la respuesta DNS** (`captive_portal.cpp`): el búfer de
  respuesta se amplió a 160 B y se añadió una comprobación defensiva. Un paquete
  DNS de 128 B con QNAME largo podía escribir fuera de un búfer de 128 B.

### Añadido
- `docs/SEGURIDAD.md`: auditoría de seguridad (hallazgos, estado y hardening).
- `docs/ROADMAP.md`: hoja de ruta de mejoras futuras.
- `docs/API.md`: referencia de los endpoints HTTP y sus parámetros.

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
