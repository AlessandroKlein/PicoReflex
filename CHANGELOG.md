# Changelog

Todas las versiones notables de PicoReflex se documentan en este archivo.
El formato sigue [Keep a Changelog](https://keepachangelog.com/) y el versionado
sigue [Semantic Versioning](https://semver.org/) (SemVer).

## [0.9.4] - 2026-10-28

### Arreglado
- **PANIC de Wi-Fi al asociarse a la red** (`sys_timeout: pool MEMP_SYS_TIMEOUT is
  empty`): `lwipopts.h` usaba el default de lwIP (`MEMP_NUM_SYS_TIMEOUT = 9`),
  insuficiente para mDNS + keepalive TCP + renovación DHCP; al asociarse el pool
  de timeouts del sistema se agotaba y lwIP panicaba. Ahora define
  `MEMP_NUM_SYS_TIMEOUT 32`.

## [0.9.3] - 2026-10-28

### Añadido
- **Log de IP de origen en cada conexión HTTP**: `http_accept` (`src/web_server.cpp`)
  imprime `[web] conn desde <ip>:<port>` por cada conexión TCP entrante. Es el
  separador definitivo para diagnosticar la web por STA: si conectando por STA
  NO aparece el log, el SYN nunca llegó al Pico (el router lo dropea por
  aislamiento de clientes); si aparece pero la página no carga, el firmware
  recibió y respondió (la respuesta se pierde en el router).

## [0.9.2] - 2026-10-28

### Arreglado
- **Reconexión en caliente AP→STA fallaba**: al conmutar del Access Point al modo
  STA sin reiniciar, `cyw43_arch_disable_ap_mode()`/`disable_sta_mode()` solo
  encolan el "link down" en el firmware CYW43 (es asíncrono); el `join` STA se
  lanzaba con la radio aún cerrando el modo anterior y el primer `connect` fallaba.
  Se añade un delay de asentamiento de 200 ms (`vTaskDelay`) en el handler
  `g_wifi_reconnect` antes de `network_up()` (en frío no ocurría porque el chip
  arranca limpio).
- **Spam "STA activa" cada segundo**: `sta_refresh_ip()` publicaba la IP en cada
  tick del watchdog de enlace aunque no hubiera cambiado. Ahora solo publica
  cuando la IP aparece por primera vez o cambia (nueva `g_last_sta_ip`); si la IP
  es 0 (DHCP en curso) o no varió, no imprime ni actualiza.

### Documentado
- `docs/COMPILAR.md`: nueva sección "Solución de problemas del STA (no carga
  `http://<IP>`)" — el HTTP escucha en `0.0.0.0:80` y no hay firewall en lwIP, así
  que la causa casi siempre es aislamiento entre clientes del router/AP
  (guest/corporativa/"AP isolation"); cómo confirmarlo y la alternativa mDNS
  `picoreflex.local`.

## [0.9.1] - 2026-10-28

### Arreglado
- **Página `/login` inalcanzable**: un `GET /login` sin `?pin=` caía en la rama de
  validación y devolvía `401 PIN incorrecto`, dejando `render_login` como código
  muerto y bloqueando el acceso a todas las páginas de configuración (no se podía
  autenticar). Ahora `src/web_server.cpp` solo valida el PIN (y consume el limitador
  anti fuerza bruta) cuando el parámetro `pin` está presente y no vacío; un `GET`
  plano de `/login` cae al render final y muestra el formulario
  (`web_ui::render("/login")` → `render_login`).

## [0.9.0] - 2026-10-28

### Añadido
- **Modo dormir configurable**: `GameConfig::sleep_enabled` (bool, por defecto
  `true`) e `idle_sleep_min` (minutos, 1..60, por defecto 3) en `include/config.hpp`;
  card "Modo dormir (ahorro de energía)" en `/display` (Habilitar/Deshabilitar +
  campo Inactividad). El protocolo de dormancia (`src/game_logic.cpp`) respeta estos
  valores (antes era un umbral fijo de 3 min) y solo dormita con el Wi-Fi apagado;
  `sleep_enabled=false` desactiva el sueño. Persistido en Flash
  (`PersistentData::sleep_enabled`/`idle_sleep_min`).

### Arreglado
- **Cuelgue en `cyw43_arch_init()`**: `lwipopts.h` no definía `TCPIP_THREAD_STACKSIZE`
  (default lwIP `0`) y la tarea `tcpip_thread` nunca se creaba. Se añade el bloque
  `#if !NO_SYS` con `TCPIP_THREAD_STACKSIZE 2560` (y `DEFAULT_THREAD_STACKSIZE`,
  `TCPIP_MBOX_SIZE`, `*_RECVMBOX_SIZE`); de vuelta a `pico_cyw43_arch_lwip_sys_freertos`
  (ver `docs/DECISIONES.md` §30).
- **"USB no reconocido" tras el bootloader**: `jump_to_app()` saltaba con IRQ
  enmascaradas (`cpsid i`); ahora replica un reset limpio (limpia NVIC/SysTick,
  fija VTOR, `cpsie i`) antes del salto (ver `docs/DECISIONES.md` §31).
- **Reset por watchdog al conectar un cliente al AP**: `src/web_server.cpp`
  (`ap_dhcp_start`/`ap_dhcp_stop`/`http_server_init`) y `src/captive_portal.cpp`
  (`captive::init`/`stop`) usaban la API raw de lwIP (`udp_new`/`udp_bind`/`udp_recv`/
  `udp_remove`) sin `cyw43_arch_lwip_begin()`/`cyw43_arch_lwip_end()`, lo que bajo
  `NO_SYS=0` disparaba una aserción de lwIP y el watchdog reiniciaba el firmware al
  conectar un cliente al AP. Ahora están envueltos en begin/end y
  `TCPIP_THREAD_STACKSIZE` sube a 2560.

### Cambiado
- **Clave del AP vuelve a ser FIJA `12345678`** (`AP_PASSWORD_DEFAULT` en
  `include/config.hpp`), igual para cualquier placa, editable desde `/wifi`
  (8..15 chars) y persistida en Flash. Se revierte la derivación del RUID de v0.7.0
  (a petición del usuario).
- `PersistentData` versión 6 (modo dormir): el primer arranque tras la actualización
  **resetea la configuración persistida**.

### Documentado
- `docs/DECISIONES.md`: §7 (clave del AP por defecto) y nueva §32 (modo dormir).
- `SECURITY.md`, `docs/SEGURIDAD.md` (§3.4), `docs/COMPILAR.md`, `docs/API.md` y
  `docs/ROADMAP.md`: corregido "derivada del RUID" → clave fija `12345678`.
- `README.md`: nota de modo dormir y clave del AP.

## [0.8.1] - 2026-10-22

### Arreglado
- **Bootloader no arrancaba la app**: *off-by-one* en la validación del MSP al saltar
  a la aplicación (`src/bootloader.c:44`) rechazaba el valor legítimo
  `__StackTop = 0x20042000` (tope exacto de la SRAM de 264 KB), por lo que el
  bootloader descartaba la imagen y volvía a modo bootloader. La cota ahora es
  `msp <= 0x20042000u` (tope incluido).

### Documentado
- Referencia al [pinout oficial de la Raspberry Pi Pico W (A4, RP-008315-DS-1)](https://pip-assets.raspberrypi.com/categories/686-raspberry-pi-pico-w/documents/RP-008315-DS-1-PicoW-A4-Pinout.pdf)
  en `docs/ESQUEMATICO.md` y `docs/ARQUITECTURA.md`.

## [0.8.0] - 2026-10-22

### Añadido
- **Soporte de display SH1106 1.3" (runtime)**: dos controladores OLED 128×64 por I2C
  (SSD1306 0.96" y SH1106 1.3") con secuencias de init separadas; selección en
  runtime desde `/display` (`?controller=0|1`) persistida en Flash. La macro
  `PICOREFLEX_DISPLAY_CONTROLLER` solo fija el valor por defecto.

### Cambiado
- `PersistentData` versión 5 (controlador de display): el primer arranque tras la
  actualización resetea la configuración persistida.

### Arreglado
- **Linker script autocontenido**: en Windows el driver g++ descarta las flags `-L`
  cuando los objetos se pasan por fichero de respuesta (`@rsp`), por lo que `ld`
  no resolvía los `INCLUDE` del linker script del SDK (`memmap_default.incl`).
  Nuevo `tools/generate_linker_script.py` genera un linker script autocontenido por
  target (ver `docs/DECISIONES.md` §29).

### Documentado
- `docs/API.md` (`/display` + parámetro `controller`) y `docs/COMPILAR.md`:
  soporte SH1106 con selección en runtime y persistencia (macro solo como valor
  por defecto).

## [0.7.0] - 2026-10-21

### Añadido
- **Watchdog hardware** (`hardware_watchdog`): `watchdog_enable(8000 ms,
  pause_on_debug=true)` al arrancar y `watchdog_update()` en ambos bucles de
  tarea; diagnóstico del último reinicio (`watchdog_caused_reboot()`).
- **Debounce de pulsadores por PIO** (`button_debounce.pio`): una state machine por
  botón que filtra el rebote en hardware (~5 ms de lockout) y notifica el flanco.
- **Soporte de display SH1106**: `display::init(Controller)` con offset de columna
  +2; selección por macro `PICOREFLEX_DISPLAY_CONTROLLER`.
- **Calibración de la curva de batería** (`BatteryCalibration`): 3 puntos (mV) +
  compensación de carga, configurable desde `/battery` y persistida en Flash.
- **Reconexión Wi-Fi en caliente (STA→AP)**: `sta_link_watchdog()` vigila el enlace
  cada 1 s; reintenta con backoff exponencial (1..15 s) y degrada a SoftAP tras 3 fallos.
- **mDNS `picoreflex.local`** (solo STA): `mdns_resp_add_netif()` + servicio `_http`
  (`pico_lwip_mdns`, `LWIP_MDNS_RESPONDER=1`, `LWIP_IGMP=1`).
- **Autenticación PIN + rate-limiting**: cookie `pr_auth` / `Authorization: Bearer`,
  PIN de compilación `WEB_PIN` (por defecto `1234`), límite global 60 req/s y 5 login/s.
- **Contraseña del AP configurable**: sin clave por defecto conocida; se deriva del
  RUID de la Flash (única por placa) o se configura desde `/wifi` (8..15 chars) y se persiste.
- **OTA A/B con bootloader de segundo nivel**: partición A/B + staging + metadatos;
  `POST /ota/upload` con CRC32 y *rollback* automático.
- **CI endurecido**: análisis estático `cppcheck` (falla el CI ante error/warning) y
  reporte de tamaño (`cmake --build build --target size` → `build/size-report.txt`).

### Cambiado
- `PersistentData` versión 3 (clave del AP) y 4 (calibración de batería): el primer
  arranque tras la actualización **resetea la configuración persistida** (récords,
  Wi-Fi, OLED y cuenta regresiva vuelven a sus valores por defecto).

### Documentado
- `docs/SEGURIDAD.md`: §3.2 (PIN + rate-limit) y §3.4 (clave del AP) resueltos;
  hallazgos `12345678` y "sin autenticación" cerrados.
- `docs/ROADMAP.md`: base actualizada a v0.7.0; ítems 1.1/1.3/1.4/1.5/1.6/1.7 y
  2.1/2.2/2.3 marcados como completados.
- `docs/API.md` (endpoints `/wifi`, `/battery`, `/ota` + autenticación PIN) y
  `docs/COMPILAR.md` (macros `PICOREFLEX_WS2812`/`PICOREFLEX_DISPLAY_CONTROLLER`/`WEB_PIN`);
  sitio MkDocs de `Docs.git` reescrito a v0.7.0.

## [0.6.1] - 2026-10-20

### Documentado
- `DECISIONES.md`: nuevas decisiones §27 (modo Simón Dice) y §28 (LEDs GPIO/WS2812);
  corregida §23 (FreeRTOS ahora integrado, no revertido).
- `ESQUEMATICO.md`: nota de la alternativa WS2812 (GPIO28 por PIO, macro
  `PICOREFLEX_WS2812`).

## [0.6.0] - 2026-10-20

### Añadido
- **Modo Simón Dice** (`GameMode::SIMON_SAYS`): secuencia de LEDs que crece. El
  jugador la reproduce; al acertar se agrega un elemento aleatorio y se repite
  (más larga); al fallar, Game Over + reinicio a longitud 1. La velocidad de
  presentación depende de la dificultad (Fácil 700 ms → Difícil 300 ms).
- **Semilla determinista en Simón Dice**: la secuencia la genera el PRNG xorshift32
  sembrado con la semilla; una misma semilla produce siempre la misma secuencia.
- **LEDs WS2812 (NeoPixel)** opcionales: nuevo driver `led_driver` con dos
  implementaciones (GPIO por defecto y WS2812 por PIO, seleccionadas con la macro
  `PICOREFLEX_WS2812`). Efectos de luces al ganar (verde) y perder (rojo).

## [0.5.1] - 2026-10-20

### Documentado
- **Referencias completas** (`docs/REFERENCIAS.md`): URLs oficiales del Pico SDK
  (hardware/runtime/networking/third-party), ejemplos de pico-examples corroborados
  (`access_point`, `access_point_wifi_provisioning`, `freertos`) y lista de ejemplos
  `pico_w/wifi`.
- `COMPILAR.md`: requisito de **FreeRTOS-Kernel** (clonarlo) documentado.

## [0.5.0] - 2026-10-20

### Añadido
- **FreeRTOS (SMP, 2 núcleos) integrado**: `game_task` + `network_task` con
  `vTaskStartScheduler()`. Wi-Fi/lwIP en `pico_cyw43_arch_lwip_sys_freertos`
  (`NO_SYS=0`). `cyw43_arch_init()` dentro de la tarea de red (patrón oficial
  pico-examples).

### Cambiado
- `sleep_ms` → `vTaskDelay` en los bucles de tarea.
- Reconexión Wi-Fi "liviana" (sin `cyw43_arch_deinit/init`).
- DORMANT degradado a sleep ligero (el DORMANT real no es compatible con FreeRTOS
  SMP sin rework).

### Documentado
- `docs/FREERTOS.md`: actualizado a "integrado" con el patrón oficial corregido.

## [0.4.7] - 2026-10-20

### Documentado
- **Docs (wiki) completo y en verde**: actualizado `AlessandroKlein/Docs` con la
  navegación completa (agregados `Esquematico` y `Freertos`), portada `Home.md`
  corregida (firmware v0.4.6, batería ADC0/GPIO26 en vez de VSYS/3, AP WPA2+DHCP)
  y enlaces rotos corregidos (`pin_defs.hpp`, `web_server.hpp` → GitHub,
  `FREERTOS.md` → `Freertos.md`). El build de MkDocs pasa en verde.

## [0.4.6] - 2026-10-20

### Documentado
- **Esquemático / diagrama circuital** (`docs/ESQUEMATICO.md` + `docs/img/esquematico.svg`):
  diagrama de bloques, tabla de conexiones, detalle por subsistema (LEDs con
  cálculo de resistencia, pulsadores, I2C, buzzer con driver, divisor de batería
  10 k/22 k, alimentación) y mapa de pines completo.

## [0.4.5] - 2026-10-20

### Documentado
- **Código**: encabezados Doxygen y comentarios "qué + por qué" completados en todo
  el código, especialmente en `dhcpserver.h/.c` (protocolo DORA, opciones RFC 2132,
  campos del mensaje BOOTP) y corrección de comentarios obsoletos en `web_ui`.
- **Wiki/Docs**: actualizados a v0.4.4 (AP WPA2, DHCP, stack del Core 1, `url_decode`
  corta en `&`, `/player` y escaneo retirados). Nuevas decisiones §24–§26 y
  dificultades del informe.

## [0.4.4] - 2026-10-20

### Cambiado
- **AP con clave WPA2 de nuevo**: el Access Point vuelve a `WPA2` con clave
  `12345678` (`AP_OPEN 0`). El fallo histórico del AP era el DHCP (faltaba),
  no el WPA2; con DHCP ya funciona con clave.

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
