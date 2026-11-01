# Estudio / Defensa — Capa de red y servidor web de PicoReflex

> Material de estudio para explicar y defender oralmente la capa de red del
> firmware PicoReflex (Raspberry Pi Pico W, Pico SDK 2.3.0 + FreeRTOS SMP).
>
> Cada módulo tiene: **(a)** qué hace, **(b)** cómo funciona con referencias a
> código real (`archivo:línea`), **(c)** por qué ese diseño, **(d)** bugs/trampas
> y su fix, y **(e)** preguntas probables de defensa con respuesta corta.
>
> Archivos de red relevantes: `src/web_server.cpp`, `src/web_ui.cpp`,
> `src/captive_portal.cpp`, `src/dhcpserver.c`, `src/ota.c`, `src/bootloader.c`,
> `lwipopts.h`, `include/ota.h`, `include/config.hpp`, `CMakeLists.txt`.

---

## 1. `network_task` — la tarea de red en el Core 1

### (a) Qué hace

FreeRTOS SMP levanta **dos tareas** con afinidad de núcleo fija:

- `game_task` → Core 0: lógica del juego en tiempo real (IRQ de botones, LEDs,
  buzzer, Flash).
- `network_task` → Core 1: Wi-Fi (`cyw43_arch`), servidor HTTP (lwIP), display
  OLED y muestreo de batería.

`network_task` se crea con **más stack y mayor prioridad** que `game_task`, y en
su cuerpo llama a `web::init()` (que hace `cyw43_arch_init()`) y luego a
`web::run()` (bucle que nunca retorna).

### (b) Cómo funciona

- `src/main.cpp:111-112`:
  ```c
  xTaskCreateAffinitySet(game_task,    "game",    2048, nullptr, tskIDLE_PRIORITY + 2, 1u << 0, nullptr);
  xTaskCreateAffinitySet(network_task, "network", 4096, nullptr, tskIDLE_PRIORITY + 3, 1u << 1, nullptr);
  ```
  → `network_task`: **4096 B de stack**, prioridad `tskIDLE_PRIORITY+3`, afinidad
  `1u<<1` = **Core 1**.
- `src/main.cpp:61-69` (`network_task`): espera el semáforo `g_game_init_sem`
  (para que `game::init()` detecte bien el flag "forzar AP"), luego `web::init()`
  y `web::run()`.
- `src/web_server.cpp:1007-1019` (`web::init()`): imprime
  `[web] cyw43_arch_init()...`, llama `cyw43_arch_init()` y `battery::init()`.
- `src/web_server.cpp:1235-1343` (`web::run()`): bucle infinito que atiende OTA,
  watchdog, dormancia, reconexión Wi-Fi, escaneo, vigilancia del enlace, parpadeo
  del LED, batería y refresco del display.

La afinidad de núcleo **no es caprichosa**: es obligatoria para que
`flash_safe_execute()` sea determinista. El comentario en `src/main.cpp:104-110`
lo explica — con `configTICK_CORE=0` el SysTick solo está en el Core 0, así que
la escritura de Flash debe caer en ese núcleo o el `__wfe()` puede colgarse.

### (c) Por qué ese diseño

`docs/DECISIONES.md` §6 y §23: se eligió FreeRTOS SMP (2 núcleos) para reemplazar
el arranque manual por `multicore_launch_core1()`. La separación de
responsabilidades (Core 0 = juego en tiempo real, Core 1 = red/IO) evita que el
bloqueo del Wi-Fi perturbe la medición de reacción en microsegundos.

**El hilo `tcpip` de lwIP (NO_SYS=0).** Con `pico_cyw43_arch_lwip_sys_freertos`
(`src/CMakeLists.txt:124`) y `NO_SYS=0` (`lwipopts.h:9-12`), lwIP corre en una
**tarea dedicada `tcpip`** de FreeRTOS, creada por `cyw43_arch_init()` →
`lwip_freertos_init()` → `tcpip_init()`. Las callbacks HTTP/DHCP/DNS se ejecutan
dentro de esa tarea `tcpip`, no en `network_task`.

### (d) Bugs/trampas y su fix — el cuelgue en `cyw43_arch_init()`

Este es **el** bug estrella de este módulo y es un clásico de defensa.

- **Síntoma:** la app se colgaba en `web::init()` justo después de imprimir
  `[web] cyw43_arch_init()...`; nunca aparecía `OK`. El LED Wi-Fi no encendía y
  no se levantaba el AP (`docs/DECISIONES.md` §30, `CHANGELOG.md` v0.9.0).
- **Causa raíz:** `cyw43_arch_init()` → `lwip_freertos_init()` → `tcpip_init()` →
  `sys_thread_new()` con `TCPIP_THREAD_STACKSIZE`. `lwipopts.h` **no definía ese
  macro**, y el default de lwIP es `0`; `xTaskCreate(stack=0)` falla, la tarea
  `tcpip_thread` **nunca se crea**, y `xSemaphoreTake(init_sem, portMAX_DELAY)` en
  `lwip_freertos.c:36` bloquea para siempre.
- **Fix:** añadir en `lwipopts.h:82-96` el bloque `#if !NO_SYS` con
  `TCPIP_THREAD_STACKSIZE` (hoy **4096**), `DEFAULT_THREAD_STACKSIZE 1024`,
  `TCPIP_MBOX_SIZE 8` y los `*_RECVMBOX_SIZE 8` (valores del ejemplo oficial
  `pico_w/wifi/freertos/iperf`).

**Importante (corrección de una idea frecuente):** el enunciado de esta sección
habla de una "migración sys_freertos → threadsafe_background (NO_SYS 0→1)", pero
el código final **no** fue en esa dirección. `threadsafe_background` (NO_SYS=1,
raw API con polling en IRQ de baja prioridad, sin tarea `tcpip`) **se probó y se
revirtió** porque rompía el arranque por completo (`docs/DECISIONES.md` §30,
`CHANGELOG.md` v0.3.1). El estado actual es `sys_freertos` (`NO_SYS=0`) con la
tarea `tcpip`; el cuelgue se arregló con el **tamaño de stack**, no cambiando de
variante. La línea real que lo confirma es `lwipopts.h:11` (`#define NO_SYS 0`) y
`src/CMakeLists.txt:124` (`pico_cyw43_arch_lwip_sys_freertos`).

### (e) Preguntas probables de defensa

1. **¿Por qué `cyw43_arch_init()` se llama dentro de `network_task` y no en
   `main()`?** — Porque es el patrón oficial de `pico-examples
   pico_w/wifi/freertos`: con `sys_freertos`, `cyw43_arch_init()` crea la tarea
   `tcpip`, y eso debe pasar *después* de `vTaskStartScheduler()`
   (`src/main.cpp:11-14`).
2. **¿Qué es la tarea `tcpip`?** — Es el hilo interno de lwIP (NO_SYS=0) donde
   corren las callbacks de red; sin ella `cyw43_arch_init()` se bloquea en el
   semáforo `init_sem`.
3. **¿Por qué `network_task` tiene más stack que `game_task`?** — Porque el Core 1
   aloja `cyw43_arch` + lwIP + HTTP; con 2 KB desbordaba (`CHANGELOG.md` v0.4.2).
4. **¿Por qué afinidad de núcleo fija?** — Para que `flash_safe_execute()` (que
   escribe Flash en el Core 0) tenga un `__wfe()` con tick periódico y no cuelgue
   (`src/main.cpp:104-110`).

---

## 2. Wi-Fi — `cyw43_arch`, STA/AP y reconexión en caliente

### (a) Qué hace

El firmware intenta conectarse como **Station (STA)** a una red Wi-Fi; si no
puede, degrada a **SoftAP** de respaldo (`PicoW-ReactionGame`, IP fija
`192.168.4.1`). Además vigila el enlace **en caliente**: si el enlace STA se cae
durante la operación, reintenta con backoff exponencial y, tras N fallos, degrada
a AP. El LED onboard indica el estado: **fijo = STA**, **parpadeo = AP**.

### (b) Cómo funciona (la máquina de estados)

Constantes en `src/web_server.cpp:70-85`: `HTTP_PORT 80`, `AP_SSID
"PicoW-ReactionGame"`, `AP_OPEN 0` (WPA2), `AP_CHANNEL 1`, `AP_IP "192.168.4.1"`,
`STA_TIMEOUT_MS 10000`, `STA_MAX_RETRIES 3`, `STA_BACKOFF_BASE_MS 1000`,
`STA_BACKOFF_MAX_MS 15000`, `STA_LINK_CHECK_MS 1000`.

Las funciones clave:

- **`sta_refresh_ip()`** (`src/web_server.cpp:1086-1097`): lee
  `cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr` y publica la IP en el estado
  compartido **solo si cambió** (dedup con `g_last_sta_ip`).
- **`sta_start()`** (`src/web_server.cpp:1101-1114`): `cyw43_arch_enable_sta_mode()`
  + `cyw43_arch_wifi_connect_timeout_ms(ssid, pass, CYW43_AUTH_WPA2_AES_PSK, 10000)`;
  si conecta → LED fijo + `sta_refresh_ip()` + `mdns_register_sta()`.
- **`ap_start()`** (`src/web_server.cpp:1117-1143`): `mdns_unregister_sta()` +
  `cyw43_arch_disable_sta_mode()` + canal fijo + `cyw43_arch_enable_ap_mode()` +
  `captive::init()` (DNS) + `ap_dhcp_start()` (DHCP).
- **`network_up()`** (`src/web_server.cpp:1149-1175`): intenta STA hasta
  `STA_MAX_RETRIES+1 = 4` veces (cada una bloquea 10 s); si todas fallan →
  `ap_start()`; en ambos casos levanta `http_server_init()`.
- **`sta_link_watchdog()`** (`src/web_server.cpp:1181-1233`): llamado cada 1 s
  desde `run()`. Consulta `cyw43_wifi_link_status(CYW43_ITF_STA)`; si
  `== CYW43_LINK_JOIN` reinicia contadores y refresca IP; si no, reintenta con
  backoff (1 s → 2 s → 4 s … tope 15 s) y tras `STA_MAX_RETRIES` fallos degrada a
  `ap_start()`.
- **Handler de reconexión** (`src/web_server.cpp:1273-1302`): cuando
  `g_wifi_reconnect` (cambio de credenciales desde `/wifi`), apaga el modo actual
  y re-ejecuta `network_up()`.

### (c) Por qué ese diseño

`docs/DECISIONES.md` §7 y §19: STA con fallback a AP garantiza que el dispositivo
**siempre** sea configurable (aunque no haya red conocida, el AP de respaldo está
ahí). La reconexión en caliente (backoff exponencial → degradación) evita que una
caída del router deje el dispositivo "muerto": eventualmente ofrece el AP. Las
credenciales se persisten en Flash (`GameConfig::wifi_ssid[33]` /
`wifi_password[65]`, `include/config.hpp:167-168`) y un cambio desde `/wifi`
dispara la reconexión.

### (d) Bugs/trampas y su fix

- **Reconexión en caliente AP→STA fallaba** (`CHANGELOG.md` v0.9.2): al conmutar
  de AP a STA sin reiniciar, `cyw43_arch_disable_ap_mode()` /
  `disable_sta_mode()` **solo encolan** el "link down" en el firmware CYW43 (es
  un ioctl **asíncrono**); el `join` STA se lanzaba con la radio aún cerrando el
  modo anterior y el primer `connect` fallaba. **Fix:** `vTaskDelay(pdMS_TO_TICKS(200))`
  de asentamiento antes de `network_up()` (`src/web_server.cpp:1291-1297`). En frío
  no ocurre porque el chip arranca limpio.
- **Spam "STA activa" cada segundo** (`CHANGELOG.md` v0.9.2): `sta_refresh_ip()`
  publicaba la IP en cada tick del watchdog aunque no hubiera cambiado. **Fix:**
  dedup con `g_last_sta_ip` (`src/web_server.cpp:1088-1090`) y reset en
  `network_up()` (`src/web_server.cpp:1155`).
- **Reset por watchdog al asociar un cliente al AP** (`CHANGELOG.md` v0.9.0): las
  llamadas a la raw API de lwIP desde `network_task` (NO el hilo `tcpip`) corrían
  sin lock y corrompían la lista `udp_pcbs`. **Fix:** envolver en
  `cyw43_arch_lwip_begin()/end()` — que en el SDK de la Pico **equivale a
  `LOCK_TCPIP_CORE`** — en `ap_dhcp_start/stop` (`src/web_server.cpp:125-139`),
  `http_server_init` (`src/web_server.cpp:989-1000`) y `captive::init/stop`
  (`src/captive_portal.cpp:120-151`). Ver el detalle del lock más abajo.
- **Cuelgue en `cyw43_arch_init()`** (v0.9.0): ya cubierto en el módulo 1.

### (e) Preguntas probables de defensa

1. **¿Qué devuelve `cyw43_wifi_link_status()` en modo STA?** — `CYW43_LINK_JOIN`
   si el join está activo; cualquier otro valor (`CYW43_LINK_FAIL/NONET/BADAUTH/
   DOWN`) indica enlace caído. Por eso el watchdog compara `!= CYW43_LINK_JOIN`
   (`src/web_server.cpp:1184-1185`).
2. **¿Por qué el delay de 200 ms en la reconexión?** — Porque
   `cyw43_wifi_set_up(...,false)` encola el "link down" de forma **asíncrona**;
   hay que dejar que el firmware CYW43 cierre el modo anterior antes de lanzar el
   `join` STA.
3. **¿Por qué `cyw43_arch_lwip_begin/end` = `LOCK_TCPIP_CORE`?** — En el SDK,
   `PICO_LWIP_CUSTOM_LOCK_TCPIP_CORE=1` redirige el lock del core lwIP al
   `async_context` del CYW43; `cyw43_arch_lwip_begin()` adquiere ese mismo
   `async_context`. Son el mismo mutex (recursivo).
4. **¿Qué pasa si fallan los 3 reintentos del watchdog?** — Degrada a `ap_start()`
   (`src/web_server.cpp:1206-1212`), que apaga STA y levanta el AP + captive
   portal + DHCP.

---

## 3. Captive portal + DHCP — capturar al cliente en modo AP

### (a) Qué hace

En modo AP, la Pico W necesita dos piezas para "capturar" al teléfono/PC y
llevarlo a la web embebida:

1. **DHCP** (`src/dhcpserver.c`): asigna una IP de la subred `192.168.4.x` al
   cliente. Sin esto, el cliente se asocia al AP pero **no obtiene IP**.
2. **Captive portal** (`src/captive_portal.cpp`): un **servidor DNS mínimo** que
   responde a toda consulta con la IP `192.168.4.1`, redirigiendo así el
   navegador a la página de configuración. Además, el servidor HTTP responde a los
   "captive portal checks" que hacen los SO (Android/iOS/Windows).

### (b) Cómo funciona

- **DHCP** (`src/dhcpserver.c`, código MIT de MicroPython): implementa el diálogo
  **DORA** (DISCOVER → OFFER → REQUEST → ACK) por UDP puerto 67
  (`src/dhcpserver.c:39-44`). El callback `dhcp_server_process`
  (`src/dhcpserver.c:221-350`) gestiona `DHCPDISCOVER` (busca IP libre) y
  `DHCPREQUEST` (confirma con `DHCPACK`). `dhcp_server_init`
  (`src/dhcpserver.c:352-367`) llama a `udp_bind_netif()` (`:365`) para **restringir
  el servidor a la interfaz del AP** (si no, respondería también por STA).
- **DNS** (`src/captive_portal.cpp:38-115`): `dns_recv_cb` arma una respuesta RFC
  1035 con `QR=1`, `RD` eco, `ANCOUNT=1`, y un registro **A** apuntando a
  `192.168.4.1` (TTL 60 s). Para consultas **AAAA** (IPv6) devuelve NODATA para
  que el cliente reintente con A (`src/captive_portal.cpp:70-71`).
- **Captive portal check** (`src/web_server.cpp:866-877`): el `http_recv` responde
  `204 No Content` a `/generate_204` (Android), `200 Success` a
  `/hotspot-detect`/`success.html` (iOS) y `Microsoft Connect Test` a
  `connecttest`/`ncsi`/`redirect` (Windows), para que el SO no descarte la red
  "sin internet".
- Arranque conjunto en `ap_start()` (`src/web_server.cpp:1141-1142`):
  `captive::init()` (DNS puerto 53) + `ap_dhcp_start()` (DHCP puerto 67).

### (c) Por qué ese diseño

`docs/DECISIONES.md` §15 y §21: el CYW43 **no trae servidor DHCP en modo AP**
(`CYW43_NETUTILS` desactivado). Sin DHCP el teléfono "conectaba y se
desconectaba" (el bug clásico de v0.2.9). El captive portal es el complemento
natural: responde DNS con la IP local y captura el "connectivity check" del SO.

### (d) Bugs/trampas y su fix

- **Faltaba el servidor DHCP** (`CHANGELOG.md` v0.2.9): el AP "conectaba y se
  desconectaba" porque el cliente no recibía IP. **Fix:** integrar `dhcpserver.c`.
- **"Se desconecta solo" del AP** (`CHANGELOG.md` v0.2.6): el SO hacía el captive
  portal check y, al no recibir lo esperado, descartaba la red. **Fix:** responder
  `204`/`Success`/`Microsoft Connect Test`.
- **Desborde de búfer en la respuesta DNS** (`CHANGELOG.md` v0.1.1): un QNAME
  largo podía escribir fuera del buffer. **Fix:** ampliar a 160 B + chequeo
  defensivo (`src/captive_portal.cpp:74-77`).
- **DNS solo A (no AAAA)** (`CHANGELOG.md` v0.2.7): responder AAAA como si fuera
  A confundía al cliente. **Fix:** NODATA para AAAA.

### (e) Preguntas probables de defensa

1. **¿Por qué hace falta un servidor DHCP propio en el AP?** — Porque el CYW43 no
   lo trae (`CYW43_NETUTILS` off); sin DHCP el cliente se asocia pero no recibe IP.
2. **¿Qué es DORA?** — La secuencia DHCP: Discover (broadcast) → Offer → Request
   → Ack (`src/dhcpserver.c:39-44`).
3. **¿Por qué `udp_bind_netif()` en el DHCP?** — Para restringir el PCB a la
   interfaz del AP y que no responda DHCP por STA (`src/dhcpserver.c:360-366`).
4. **¿Cómo "captura" el navegador?** — Resuelve cualquier dominio a `192.168.4.1`
   (DNS) y el HTTP responde a los checks de conectividad para que el SO no abandone
   la red.

---

## 4. mDNS — `picoreflex.local`

### (a) Qué hace

Publica el nombre de host **`picoreflex.local`** en modo STA, para poder acceder a
la web escribiendo `http://picoreflex.local` sin conocer la IP asignada por DHCP.
En modo AP no se registra (ahí la IP es fija `192.168.4.1`).

### (b) Cómo funciona

- Constantes en `src/web_server.cpp:91-93`: `MDNS_HOSTNAME "picoreflex"`,
  `MDNS_SERVICE_NAME "PicoReflex"`, `MDNS_SERVICE_TYPE "_http"`.
- `mdns_init_once()` (`src/web_server.cpp:1038-1044`): llama `mdns_resp_init()`
  una sola vez (socket UDP 5353).
- `mdns_register_sta()` (`src/web_server.cpp:1049-1067`): `mdns_resp_add_netif()`
  con el hostname + `mdns_resp_add_service()` (servicio `_http`, TCP puerto 80).
  En reconexiones re-anuncia con `mdns_resp_announce()` (evita el "Double add").
- `mdns_unregister_sta()` (`src/web_server.cpp:1070-1076`): `mdns_resp_remove_netif()`
  al degradar a AP.
- Configuración en `lwipopts.h:57-62`: `LWIP_IGMP 1` (multicast IPv4
  `224.0.0.251`), `LWIP_MDNS_RESPONDER 1` y `LWIP_NUM_NETIF_CLIENT_DATA 1`
  (slot por netif que el responder usa para el `struct mdns_host`).
- Librería enlazada en `src/CMakeLists.txt:126`: `pico_lwip_mdns`.

### (c) Por qué ese diseño

El mDNS (RFC 6762) es la alternativa "sin DNS estático" para encontrar el
dispositivo en una red con DHCP: el cliente resuelve `picoreflex.local` por
multicast. Requiere IGMP para unirse al grupo multicast y un slot de client-data
por netif. El servicio `_http._tcp` permite además que herramientas de
descubrimiento (Bonjour/Avahi) lo listeen.

### (d) Bugs/trampas y su fix

- **PANIC `sys_timeout: pool MEMP_SYS_TIMEOUT is empty`** (`CHANGELOG.md` v0.9.4):
  el default de lwIP (`MEMP_NUM_SYS_TIMEOUT = 9`) no contaba los sondeos/anuncios
  del mDNS ni el keepalive TCP; al asociarse, el pool se agotaba y lwIP panicaba.
  **Fix:** `MEMP_NUM_SYS_TIMEOUT 32` (`lwipopts.h:74`).
- **`mdns.c` no compila sin `LWIP_IGMP`**: el responder exige IGMP para IPv4
  (`LWIP_IGMP 1`) o falla el `#error` de compilación.
- **"Double add" al reconectar**: `mdns_resp_add_netif` aserta si el netif ya está
  registrado; por eso `mdns_register_sta()` re-anuncia en vez de re-añadir
  (`src/web_server.cpp:1053-1055`).

### (e) Preguntas probables de defensa

1. **¿Qué puerto y protocolo usa mDNS?** — UDP 5353, multicast a `224.0.0.251`.
2. **¿Por qué `LWIP_IGMP 1`?** — Para que lwIP haga `igmp_joingroup` al grupo
   multicast de mDNS.
3. **¿Por qué no se registra en AP?** — No hace falta: la IP es fija `192.168.4.1`.
4. **¿Por qué `MEMP_NUM_SYS_TIMEOUT 32`?** — Porque el default no alcanza para
   mDNS + keepalive + renovación DHCP a la vez.

---

## 5. Servidor web — lwIP raw API y las rutas HTTP

### (a) Qué hace

Sirve una web completa embebida (sin servidor externo): control del juego
(`/start`, `/stop`, `/reset`), configuración (`/config`, `/game`, `/display`,
`/wifi`, `/battery`, `/seed`), autenticación (`/login`, `/logout`), escaneo
(`/scan`), actualización OTA (`/ota`, `/ota/upload`) y la Home (`/`).

### (b) Cómo funciona

**Backend HTTP con la raw API de lwIP** (no sockets):

- `http_server_init()` (`src/web_server.cpp:989-1000`): `tcp_new_ip_type(IPADDR_TYPE_ANY)`
  + `tcp_bind(IP_ADDR_ANY, 80)` + `tcp_listen` + `tcp_accept` — escucha en
  `0.0.0.0:80` (sirve igual en STA y AP), bajo `cyw43_arch_lwip_begin/end`.
- `http_accept()` (`src/web_server.cpp:981-987`): deshabilita Nagle
  (`tcp_nagle_disable`) y registra `http_err` + `http_recv`.
- `http_recv()` (`src/web_server.cpp:799-979`) es el router: copia la petición a
  un buffer estático (`request[1024]`, `:827`), extrae ruta + query (`:841-853`),
  aplica rate-limit (`:856`), responde captive-portal checks (`:866-877`),
  `/scan` (`:887-902`), autenticación (`:907-910`), `/login`/`/logout`
  (`:913-942`), rutas de control (`:945-953`) y páginas de configuración
  (`:954-960`), y responde con `tcp_write` + `tcp_output` (`:963-977`).
- **Respuesta HTTP**: `send_http_response()` (`:567-586`) y `send_redirect()`
  (`:590-598`) arman cabeceras + `Content-Length` y cierran con
  `Connection: close`; `http_sent()` (`:558-561`) hace `tcp_close` al confirmar
  el envío.
- Buffer global de respuesta: `g_body[8192]` (`src/web_server.cpp:187-189`), porque
  la Home con CSS mide ~4.4 KB.

**Frontend separado** (`src/web_ui.cpp`): `web_ui::render(path, out, len)`
(`src/web_ui.cpp:603-631`) despacha por ruta a `render_home/render_seed/render_game/
render_display/render_wifi/render_battery/render_login/render_ota`. El HTML se arma
con un mini "string builder" (`Builder`, `:68-94`) y un CSS oscuro compartido
(`kCSS`, `:103-145`). Todo texto controlado por el usuario se escapa con
`html_escape()` (`:39-61`) contra XSS.

**Aplicación de config** (`src/web_server.cpp:418-550`): `handle_config` parsea la
query, aplica a `GameConfig` y — solo si `changed` — llama `config_set` +
`control_send(CONFIG_CHANGED)` + `g_wifi_reconnect` si cambió Wi-Fi.

### (c) Por qué ese diseño

`docs/DECISIONES.md` §8 y §17: la **raw API** de lwIP (sin `LWIP_NETCONN` ni
sockets) es la opción más liviana en RAM para un microcontrolador; el modelo
`NO_SYS=0` deja las callbacks en la tarea `tcpip`. La separación backend
(`web_server.cpp`, ruteo/parseo) / frontend (`web_ui.cpp`, solo HTML) desacopla
lógica de red de presentación y centraliza el escape XSS.

### (d) Bugs/trampas y su fix

- **Página web en negro** (`CHANGELOG.md` v0.3.3): el buffer de 2048 B truncaba el
  HTML (~4.4 KB). **Fix:** `g_body[8192]`.
- **Truncado de SSID/contraseña largos** (`CHANGELOG.md` v0.4.2): `path[128]`
  recortaba la query. **Fix:** `request[1024]` + buffers estáticos.
- **`tcp_recved()` ausente en GET** (`CHANGELOG.md` v0.9.5): sin anunciar la
  ventana recibida, lwIP deja de aceptar datos. **Fix:** `tcp_recved(tpcb, recv_len)`
  (`src/web_server.cpp:833`).
- **Crash al navegar rápido + use-after-free al abortar OTA** (`CHANGELOG.md`
  v0.9.5): doble free / pcb liberado reutilizado. **Fix:** callback `http_err`
  (`:788-796`) que limpia el estado OTA.
- **Escritura de Flash al solo *ver* una página** (`CHANGELOG.md` v0.9.5): visitar
  `/display` disparaba `flash_safe_execute()`. **Fix:** `handle_config` retorna
  temprano si no hay query (`:427`).
- **XSS almacenado** (`CHANGELOG.md` v0.1.3): el `name` se insertaba sin escapar.
  **Fix:** `html_escape()`.

### (e) Preguntas probables de defensa

1. **¿Por qué raw API y no sockets?** — Menor footprint de RAM; `LWIP_SOCKET 0` /
   `LWIP_NETCONN 0` (`lwipopts.h`), y con `NO_SYS=0` las callbacks corren en la
   tarea `tcpip`.
2. **¿Cómo se parsea una petición?** — `pbuf_copy_partial` a `request[1024]`,
   luego se extrae la ruta entre "GET " y el espacio, y la query tras `?`
   (`src/web_server.cpp:841-853`).
3. **¿Qué hace `http_sent`?** — Cierra la conexión (`tcp_close`) cuando los datos
   ya fueron confirmados (ACK), porque el servidor usa `Connection: close`.
4. **¿Por qué `tcp_nagle_disable`?** — Para no retardar respuestas HTTP cortas
   (Nagle agrupa segmentos pequeños).
5. **¿Cómo se previene XSS?** — `web_ui::html_escape()` sobre SSID/password antes
   de insertarlos en el HTML.

---

## 6. OTA — actualización A/B con staging + CRC32

### (a) Qué hace

Actualiza el firmware por aire sin cable: la web sube un `.bin` a un área de
**staging**, se verifica su **CRC32**, y al reiniciar un **bootloader de segundo
nivel** copia la imagen verificada al slot de aplicación (con **rollback** si
falla). Además, la página `/ota` permite "Comprobar actualizaciones" comparando la
versión runtime con la última release de GitHub.

### (b) Cómo funciona

**Mapa de Flash** (`include/ota.h:5-25` y `:43-47`):

| Offset | Tamaño | Uso |
|---|---|---|
| `0x00000000` | 64 KB | Bootloader de segundo nivel (`0x10000000`) |
| `0x00010000` | 896 KB | Slot de APLICACIÓN (`0x10010000`) |
| `0x000F0000` | 896 KB | Slot de STAGING (`0x100F0000`) |
| `0x001E0000` | 4 KB | Metadatos OTA (`0x101E0000`) |
| `0x001FF000` | 4 KB | Datos persistentes (config) |

- `OTA_SLOT_SIZE 0xE0000` (917504 B) y `OTA_SLOT_VECTOR 0x100` (la tabla de
  vectores está 256 B tras el boot2).

**Subida por POST** (`src/web_server.cpp:600-796`): el body binario se recibe por
streaming; las cabeceras se acumulan hasta `\r\n\r\n`, se leen `Content-Length` y
`X-CRC32` (`:747-753`), y el cuerpo se graba **sector a sector (4 KB)** en staging
calculando el CRC en caliente (`ota_stream_chunk`, `:660-680`). Al terminar,
`ota_finish` (`:684-727`) compara CRC + longitud; si OK escribe metadatos
`commit_state=PENDING` y reinicia; si no, responde `422` y **no conmuta**.

**CRC32** (`src/ota.c:21-30`): IEEE 802.3, polinomio reflejado `0xEDB88320`,
API incremental (`crc = ota_crc32(crc, chunk, len)`).

**Metadatos** (`include/ota.h:64-75`): `ota_metadata_t` con `magic "OTA1"`,
`version`, `commit_state` (IDLE/PENDING), `staging_len/crc32`, `app_len/crc32` y
`checksum`. Se validan con `ota_meta_valid` (`src/ota.c:41-52`).

**Bootloader** (`src/bootloader.c:108-178`): enlazado en `0x10000000`, mononúcleo,
sin FreeRTOS/lwIP. Si hay `commit PENDING`, verifica el CRC del staging
(`ota_image_verify`), lo **copia** al slot de app (`flash_copy`, `:84-103`) y
verifica la copia antes de conmutar; si el staging está corrupto, limpia el commit
y arranca la imagen anterior (**rollback**). `jump_to_app()` (`:42-79`) valida
MSP (en RAM) y reset (en Flash) antes de saltar, y deja las IRQ habilitadas
(`cpsie i`) para que el USB de la app enumere.

**Comprobar actualizaciones** (`src/web_ui.cpp:526-595`): como la Pico W **no tiene
TLS**, es el **navegador** quien habla con `api.github.com/repos/.../releases/latest`
(vía `fetch`), compara con `PICOREFLEX_VERSION` (inyectada por CMake) y ofrece el
enlace de descarga. La subida la hace el navegador con `fetch('/ota/upload', ...)`
calculando el CRC32 en JavaScript (`:557-569`).

**Versión única** (`src/CMakeLists.txt:85` y `:105`): `PICOREFLEX_VERSION "0.9.7"`
es la única fuente de verdad, inyectada como `-DPICOREFLEX_VERSION` y en los
metadatos del `.uf2`.

### (c) Por qué ese diseño

La partición **staging + commit** es la forma estándar de hacer OTA segura en un
MCU con Flash limitada: la imagen nueva se descarga y verifica **antes** de tocar
la imagen que corre, de modo que un corte de luz a mitad de la descarga o un CRC
incorrecto nunca deja el dispositivo "bricked". El bootloader hace el commit
atómico (verificar la copia antes de conmutar) y el rollback. La app se enlaza
**siempre** en `0x10010000` (`src/CMakeLists.txt:153-162`) para que el bootloader
pueda conmutar sin re-vincular.

### (d) Bugs/trampas y su fix

- **Bootloader no arrancaba la app (off-by-one)** (`CHANGELOG.md` v0.8.1): la
  validación del MSP rechazaba el valor legítimo `0x20042000` (tope exacto de la
  SRAM). **Fix:** cota `msp <= 0x20042000u` (`src/bootloader.c:51`).
- **"USB no reconocido" tras el bootloader** (`CHANGELOG.md` v0.9.0): `jump_to_app`
  saltaba con `cpsid i` (IRQ enmascaradas), así que la IRQ USB no podía disparar
  durante `stdio_init_all`. **Fix:** limpiar NVIC/SysTick + `cpsie i` antes del
  salto (`src/bootloader.c:67-77`).
- **Use-after-free al abortar una subida** (`CHANGELOG.md` v0.9.5): cancelar la
  subida dejaba `g_ota_pcb` apuntando a un pcb liberado. **Fix:** callback
  `http_err` limpia el estado (`src/web_server.cpp:788-796`).
- **CRC incorrecto** → `422 Unprocessable Content` y **no** conmuta
  (`src/web_server.cpp:696-700`).

### (e) Preguntas probables de defensa

1. **¿Por qué staging y no grabar directo en el slot de app?** — Para no corromper
   la imagen que corre: se verifica el CRC en staging y el bootloader solo copia
   tras validar (rollback si falla).
2. **¿Qué pasa si el CRC no coincide?** — La app responde `422` y **no** escribe
   metadatos de commit; sigue corriendo la imagen actual.
3. **¿Cómo "Comprobar actualizaciones" si la Pico no tiene TLS?** — La consulta a
   la API de GitHub la hace el **navegador** por HTTPS; el firmware solo expone su
   versión (`src/web_ui.cpp:572-592`).
4. **¿Qué es `jump_to_app` y por qué `cpsie i`?** — El salto del bootloader a la
   app replicando un reset limpio (NVIC/SysTick/VTOR), con IRQ habilitadas para
   que el USB enumere.
5. **¿Dónde está la tabla de vectores de la app?** — En `slot_base + 0x100`
   (tras el boot2 de 256 B), `OTA_SLOT_VECTOR 0x100` (`include/ota.h:51`).

---

## 7. Auth + rate-limiting — PIN, cookie y el bug de `/login`

### (a) Qué hace

Protege los endpoints de **control y configuración** con un **PIN** (por defecto
`1234`), con **rate-limiting** anti fuerza bruta, y gestiona una sesión por
**cookie**. La Home y el captive portal no requieren PIN.

### (b) Cómo funciona

- **PIN** (`src/web_server.cpp:256-258`): `#define WEB_PIN "1234"`, sobrescribible
  en compilación con `-DWEB_PIN=...`.
- **Rate-limiting** (`src/web_server.cpp:261-264` y `:399-408`): ventana fija
  global `RATE_MAX_REQS 60`/s y de login `LOGIN_MAX_REQS 5`/s
  (`rate_limited()`). Como el servidor es de un solo hilo (callbacks serializadas),
  no necesita exclusión mutua (`:266-267`).
- **Token de sesión** (`src/web_server.cpp:277-289`): `auth_token()` = FNV-1a del
  PIN XOR con una sal (`0x9e3779b9`), en hex de 8 caracteres. La cookie no expone
  el PIN en claro.
- **Comparación en tiempo constante** (`src/web_server.cpp:292-303`): `pin_equal()`
  evita el timing side-channel al validar el PIN.
- **Validación** (`src/web_server.cpp:343-382`): `is_authorized()` acepta
  `Cookie: pr_auth=<token>` o `Authorization: Bearer <PIN>`. `is_protected_path()`
  (`:385-396`) lista las rutas protegidas: `/start /stop /reset /seed /game
  /display /wifi /battery /config /ota`.
- **Flujo** (`src/web_server.cpp:907-942`): si una ruta protegida no está
  autorizada → `302` a `/login`. `/login?pin=...` valida el PIN (consumiendo el
  limitador de login), setea la cookie `pr_auth` y redirige a `/`; PIN incorrecto
  → `401`. `/logout` borra la cookie (`Max-Age=0`).

### (c) Por qué ese diseño

`docs/SEGURIDAD.md` §3.2 (y `CHANGELOG.md` v0.7.0): la web está pensada
para operarse en una LAN de confianza, así que el PIN no es autenticación fuerte,
sino una barrera anti uso accidental/malintencionado. La cookie derivada (FNV-1a +
sal) evita mandar el PIN en cada request; el rate-limit mitiga fuerza bruta; la
comparación constante mitiga timing.

### (d) Bugs/trampas y su fix

- **Página `/login` inalcanzable** (`CHANGELOG.md` v0.9.1): un `GET /login` sin
  `?pin=` caía en la rama de validación y devolvía `401 PIN incorrecto`, dejando
  `render_login` como código muerto → **no se podía autenticar** (bucle: pides
  config → 302 a `/login` → `/login` da 401 → no hay formulario). **Fix:** solo
  validar el PIN cuando el parámetro `pin` está presente y no vacío
  (`src/web_server.cpp:913-916`); un `GET` plano de `/login` cae al render del
  formulario.
- **401 crudo en texto plano** (`src/web_server.cpp:904-906`): un 401 `text/plain`
  se pintaba como error y parecía que "la página no cargaba". **Fix:** redirigir
  `302` a `/login`.

### (e) Preguntas probables de defensa

1. **¿Por qué 302 y no 401 en rutas protegidas?** — Porque un 401 `text/plain` se
   pinta como error en el navegador; el 302 lleva al formulario de login
   (`src/web_server.cpp:907-909`).
2. **¿Qué es la cookie `pr_auth`?** — Un token FNV-1a(PIN) XOR sal en hex, no el
   PIN en claro (`src/web_server.cpp:283-289`).
3. **¿Por qué `pin_equal` en tiempo constante?** — Para no filtrar el PIN por
   timing (compara byte a byte con `diff |= ca ^ cb`).
4. **¿Cómo funciona el rate-limit?** — Ventana fija de 1 s con contador; superado
   el límite devuelve `429 Too Many Requests` (`src/web_server.cpp:399-408`).
5. **¿Cuál era el bug de `/login`?** — Un `GET /login` sin `?pin=` devolvía `401`
   en vez del formulario, bloqueando la autenticación (`CHANGELOG.md` v0.9.1).

---

## Apéndice — mapa rápido de archivos de red

| Archivo | Rol |
|---|---|
| `src/web_server.cpp` | Backend HTTP (raw lwIP), STA/AP, mDNS, OTA, auth, bucle `run()` |
| `src/web_ui.cpp` | Frontend HTML/CSS/JS (rutas → páginas) |
| `src/captive_portal.cpp` | Servidor DNS mínimo (puerto 53) del captive portal |
| `src/dhcpserver.c` | Servidor DHCP del AP (DORA, puerto 67) |
| `src/ota.c` / `include/ota.h` | CRC32 + metadatos + verificación (compartido app/bootloader) |
| `src/bootloader.c` | Bootloader de segundo nivel (staging → commit / rollback) |
| `lwipopts.h` | Configuración de lwIP (NO_SYS, TCPIP stack, mDNS, IGMP, timeouts) |
| `include/config.hpp` | `GameConfig` (credenciales Wi-Fi), `SystemStatus`, eventos |
| `CMakeLists.txt` | Librerías (sys_freertos, mDNS), versión, linker OTA |
