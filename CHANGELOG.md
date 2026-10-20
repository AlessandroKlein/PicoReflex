# Changelog

Todas las versiones notables de PicoReflex se documentan en este archivo.
El formato sigue [Keep a Changelog](https://keepachangelog.com/) y el versionado
sigue [Semantic Versioning](https://semver.org/) (SemVer).

## [0.4.3] - 2026-10-20

### Corregido (causa raíz del guardado de Wi-Fi)
- **`url_decode` no se detenía en `&`**: al guardar, el SSID capturaba toda la
  query restante (`"ssid&pass=..."`) y la contraseña quedaba "sola" → el SSID
  guardado era inválido y la reconexión fallaba. Ahora `url_decode` corta en `&`.

### Eliminado
- Botón **"Buscar redes"** (y su JavaScript/datalist): el escaneo del CYW43 no
  devolvía redes de forma fiable, así que se quitó de `/wifi`.

## [0.4.2] - 2026-10-20

### Corregido (causa raíz del reinicio al guardar/escanear)
- **Stack del Core 1 insuficiente**: el servidor web corría con 2 KB de stack
  (`PICO_CORE1_STACK_SIZE` = 0x800 por defecto) y desbordaba al reconectar o
  escanear. Se amplió a 4 KB (`0x1000`).
- **Truncado de SSID/contraseña largos**: el buffer `path[128]` recortaba la query
  (SSID + contraseña URL-encoded ~300 B). Se amplió a `1024` B y los buffers de
  petición ahora son estáticos (no consumen stack).

## [0.4.1] - 2026-10-20

### Corregido
- **Reinicio al guardar Wi-Fi**: al guardar credenciales, la escritura a Flash
  (Core 0) y la reconexión `cyw43_arch_deinit/init` (Core 1) corrían a la vez y
  crasheaban. Ahora la reconexión **espera** (`g_persist_done`) a que termine la
  persistencia antes de tocar el CYW43.
- **Diagnóstico del escaneo**: `wifi_scan_now()` imprime por serial el resultado
  (`rc` y cantidad de redes) para depurar el botón "Buscar redes".

## [0.4.0] - 2026-10-20

### Añadido
- **Escaneo de redes Wi-Fi** en `/wifi`: botón "Buscar redes" que llena un
  autocompletado (datalist) con los SSIDs detectados (`/scan`). Solo queda
  escribir la contraseña.
- **Mostrar/ocultar contraseña** en `/wifi` (checkbox).

### Eliminado
- Página `/player`: no había modo multijugador, así que el nombre de jugador dejó
  de ser configurable por web (el campo interno `user_name` se mantiene con el
  valor por defecto "JUGADOR").

### Corregido
- `/favicon.ico` devuelve 404 rápido (antes el navegador pedía el favicon y
  demoraba/tildaba la carga de la página).

## [0.3.3] - 2026-10-20

### Corregido
- **Página web en negro**: el buffer de respuesta HTTP era de 2048 B y el HTML
  nuevo (CSS + contenido) mide ~4.4 KB, por lo que la página se truncaba y
  quedaba vacía. Se amplió a 8192 B.

### Documentado
- `COMPILAR.md`: explicación de **qué es la fuente 5x7**, por qué se regenera con
  `tools/gen_font.py` y cómo funciona su formato (95 glifos × 5 bytes, bit 0 = fila
  superior).

## [0.3.2] - 2026-10-20

### Corregido
- **La web dejaba de responder**: el modo DORMANT se activaba a los 3 min sin
  tocar botones y apagaba el Wi-Fi, dejando el servidor HTTP inaccesible. Ahora
  **no se duerme mientras el Wi-Fi está activo** (STA o AP); solo entra en
  DORMANT cuando no hay red (ahorro de energía real sin cortar la web).

## [0.3.1] - 2026-10-20

### Revertido
- **FreeRTOS** (intento de v0.3.0): rompía el arranque del Wi-Fi (sin LED, sin
  AP, sin STA). Se vuelve al dual-core manual (`multicore_launch_core1`) y a
  `pico_cyw43_arch_lwip_threadsafe_background` (`NO_SYS=1`), que funciona.
  El análisis queda en `docs/FREERTOS.md` para reintentarlo con la corrección
  (`cyw43_arch_init()` en `main()` antes del scheduler) y verificación en HW.

### Conservado
- Servidor DHCP del AP (fix de "conecta y desconecta") y frontend mejorado
  (CSS + ejemplo de semilla) de v0.3.0.

## [0.3.0] - 2026-10-20

### Añadido
- **FreeRTOS (SMP, 2 núcleos)**: reemplaza el arranque por `multicore_launch_core1`
  con tareas `game_task` y `network_task` y `vTaskStartScheduler()`. Ver
  `docs/FREERTOS.md` para el análisis completo.
- **Frontend mejorado**: CSS moderno (variables, paleta oscura, navegación por
  pestañas, tabla con filas alternadas) y página `/seed` con ejemplo de semilla
  (rango 0–4294967295, 0 = aleatoria).

### Cambiado
- Wi-Fi/lwIP: `pico_cyw43_arch_lwip_threadsafe_background` →
  `pico_cyw43_arch_lwip_sys_freertos` (`NO_SYS=0`, tarea `tcpip`).
- `sleep_ms` → `vTaskDelay` en los bucles de tarea.

## [0.2.9] - 2026-10-20

### Corregido (raíz del problema del AP)
- **Faltaba el servidor DHCP en modo AP**: el teléfono se asociaba a
  `PicoW-ReactionGame` pero nunca recibía IP, por eso "conectaba y se
  desconectaba". Se integró `dhcpserver` (MicroPython/pico-examples, MIT), que
  asigna IPs `192.168.4.16`–`192.168.4.23` a los clientes del AP.

## [0.2.8] - 2026-10-20

### Añadido
- **UART de depuración** habilitado (UART0 en GPIO0=TX / GPIO1=RX, 115200 baud)
  como respaldo al USB CDC para ver el `printf` de estado de red.
- Versión del firmware corregida en el binario (antes quedó en 0.2.0).

## [0.2.7] - 2026-10-20

### Corregido
- **DNS del captive portal**: ahora responde solo consultas A (IPv4) con la IP del
  dispositivo y devuelve NODATA para AAAA (IPv6), para que el *captive portal check*
  del teléfono no falle y descarte la red.

## [0.2.6] - 2026-10-20

### Corregido
- **"Se desconecta solo" del AP**: el teléfono hace un *captive portal check*
  (GET a `/generate_204` en Android, `/hotspot-detect.html` en iOS, etc.) y, al no
  recibir la respuesta esperada, descarta la red "sin internet". El servidor ahora
  responde `204`/`Success`/`Microsoft Connect Test` para que el teléfono mantenga
  la conexión.
- **Canal del AP fijo** en 1 (`cyw43_wifi_ap_set_channel`): evita inestabilidad del
  auto-canal del CYW43439.

## [0.2.5] - 2026-10-20

### Corregido
- **AP abierto por defecto**: el Access Point ahora es **abierto (sin clave)**.
  El AP WPA2 del CYW43 daba "No fue posible conectarse a la red" en varios
  teléfonos. Se puede volver a WPA2 con `AP_OPEN 0` en `src/web_server.cpp`.

## [0.2.4] - 2026-10-20

### Añadido
- **Forzar modo AP**: mantener el botón 0 al encender/reiniciar salta el intento
  de STA y arranca directo en `PicoW-ReactionGame` (flag `g_force_ap`).
- **Diagnóstico por USB**: `printf` del estado de red (STA/AP, IP, credenciales)
  para depurar la conexión.

### Documentado
- `COMPILAR.md`: nueva sección "Solución de problemas del AP" (forzar AP, esperar
  10 s, alimentación por USB, LED indicador).

## [0.2.3] - 2026-10-20

### Corregido
- **Conexión al Access Point**: se cambió la autenticación de `WPA2_AES_PSK` a
  `WPA2_MIXED_PSK` para mejorar la compatibilidad con más clientes (algunos
  teléfonos no se conectaban con la clave `12345678`).

### Documentado
- Credenciales del AP (`PicoW-ReactionGame` / `12345678` / IP `192.168.4.1`)
  documentadas en `COMPILAR.md` (sección "Usar") y `DECISIONES.md`.

## [0.2.2] - 2026-10-20

### Añadido
- **Tabla de tiempos con contexto**: la tabla "Últimos tiempos de reacción" del
  Home ahora muestra, por cada reacción, la **semilla** usada y la **dificultad**
  (además del tiempo en ms). El historial (`GameStats.history`) pasó de guardar
  solo `uint32_t` a una estructura `HistoryEntry { time_ms, seed, difficulty }`.

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
