# Changelog

Todas las versiones notables de PicoReflex se documentan en este archivo.
El formato sigue [Keep a Changelog](https://keepachangelog.com/) y el versionado
sigue [Semantic Versioning](https://semver.org/) (SemVer).

## [0.2.1] - 2026-10-20

### Corregido
- **Medición de batería**: se reemplazó el divisor interno VSYS/3 por un
  **divisor externo** (R1 = 10 kΩ batería→ADC, R2 = 22 kΩ ADC→GND) en **GPIO26
  (ADC0)**. Conversión `Vbat = Vpin · 32/22`. GPIO26 está libre en la Pico W
  (evita el conflicto de GPIO29 con el Wi-Fi).

### Añadido
- **Test del frontend en el host** (`tools/web_preview/`): compila `web_ui.cpp`
  con mocks y genera el HTML de las 6 rutas + prueba de escape anti-XSS.

## [0.2.0] - 2026-10-20

### Añadido
- **Disparador de inicio con cuenta regresiva**: la ronda arranca presionando DOS
  botones a la vez, seguido de una cuenta regresiva configurable (`start_delay_s`,
  por defecto 3 s, máx 10 s) con beeps.
- **Páginas web separadas** (frontend en `web_ui.cpp`): Home con **tabla de tiempos
  de reacción**, y páginas de configuración `/seed`, `/game`, `/display`, `/wifi`,
  `/player` con navegación común.
- **Wi-Fi configurable desde la web** (`/wifi`): guarda SSID/contraseña, las
  persiste en Flash y re-conecta automáticamente.
- **OLED deshabilitable** (`/display`): apaga el display (comando 0xAE); toda la
  información sigue disponible en la web.
- **Historial de reacción** (`GameStats.history[20]`) para la tabla del Home.
- **Persistencia ampliada** (`PersistentData` v2): Wi-Fi, OLED y cuenta regresiva.

### Cambiado
- `web_server.cpp` divide backend (ruteo/parseo/reconexión) del frontend HTML
  (`web_ui.cpp`), vía `web_ui::render()`.
- Todo el sistema (web, DNS, configuración) es 100 % embebido en la Pico W.

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
