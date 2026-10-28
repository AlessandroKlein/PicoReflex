---
tags:
  - picoreflex
  - roadmap
  - mejoras
---

# Hoja de Ruta de Mejoras — PicoReflex

> **Tipo:** Planificación | **Estado:** Propuesta | **Fecha:** 2026-10-21 | **Base:** v0.7.0
>
> Prioridades: **P0** imprescindible para operación real · **P1** valor alto ·
> **P2** deseable · **P3** opcional/investigación.

> **Completado desde la base original (v0.1.1) hasta v0.7.0:**
> - Credenciales Wi-Fi configurables desde la web y **persistidas en Flash** (1.2 y 2.2).
> - Re-conexión Wi-Fi en caliente al cambiar credenciales (1.3, parcial).
> - **Servidor DHCP del AP** (fix "conecta y se desconecta") — `src/dhcpserver.c`.
> - **AP con WPA2** (clave por defecto `12345678` configurable, canal 1) y **captive portal**.
> - **Stack de la tarea de red = 4 KB** (fix de *hard fault* al guardar/escanear).
> - **Buffer HTTP 8192 B** (la Home con CSS mide ~4.4 KB).
> - Frontend con CSS moderno; `/player` eliminado; botón "Buscar redes" eliminado.
> - **FreeRTOS (SMP, 2 núcleos) integrado** — ver `docs/FREERTOS.md`.
> - **Modo Simón Dice** (`GameMode::SIMON_SAYS`) — ver `CHANGELOG.md` v0.6.0.
> - **LEDs WS2812 (NeoPixel) opcionales** (macro `PICOREFLEX_WS2812`) — v0.6.0.
>
> **Nuevo en v0.7.0 (tareas t1–t10):**
> - **Watchdog hardware** (1.1) — `watchdog_enable(8000 ms)` + `watchdog_update()`.
> - **Debounce por PIO** (2.1) — `button_debounce.pio`, una state machine por botón.
> - **Soporte SH1106** (1.6) — selección en runtime desde `/display` y persistencia
>   (`PICOREFLEX_DISPLAY_CONTROLLER` solo fija el valor por defecto).
> - **Calibración de batería** (1.7) — `BatteryCalibration` configurable desde `/battery`.
> - **Reconexión STA→AP en caliente** (1.3 y 2.3) — `sta_link_watchdog()`.
> - **mDNS `picoreflex.local`** (1.4) — `LWIP_MDNS_RESPONDER` + `pico_lwip_mdns`.
> - **Autenticación PIN + rate-limiting** — `WEB_PIN` + cookie/Bearer (SEGURIDAD.md 3.2).
> - **Clave del AP configurable/persistida** — por defecto `12345678`, editable desde `/wifi` (SEGURIDAD.md 3.4).
> - **OTA A/B con bootloader** (1.5) — `POST /ota/upload` + rollback.
> - **CI endurecido** — `cppcheck` + reporte de tamaño (`target size`).

Esta hoja de ruta surge del análisis del código actual (módulos `game_logic`,
`web_server`, `battery_monitor`, `display_driver`, `flash_storage`, `audio`,
`captive_portal`, `dhcpserver`, `main`, `shared_state`) y de la documentación
oficial del [Pico SDK](https://www.raspberrypi.com/documentation/pico-sdk/index_doxygen.html)
y [pico-examples](https://github.com/raspberrypi/pico-examples).

---

## 1. Mejoras de funcionalidad

### 1.1 [P0] ✅ Watchdog timer — reinicio automático ante cuelgues

- **Qué:** habilitar `hardware_watchdog` para que, si el firmware se cuelga
  (p. ej. un bucle de la pila lwIP, una IRQ que no retorna o un deadlock entre
  núcleos), la Pico W se reinicie sola. Hoy **no hay watchdog**: un cuelgue deja
  el dispositivo muerto hasta un *power-cycle* manual.
- **Cómo:** `watchdog_enable(timeout_ms, true)` al arrancar y `watchdog_update()`
  periódicamente en los bucles de ambos núcleos (o en el bucle del Core 0 que
  siempre corre). Conviene además leer el motivo del último arranque con
  `watchdog_enable_caused_reboot()` para distinguir un reinicio por watchdog.
- **Por qué:** es la red de seguridad estándar en sistemas embebidos autónomos;
  sin supervisión, un bug en el entorno de red puede dejar el juego inoperable.
- **Referencias:**
  - [hardware_watchdog (Pico SDK)](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_watchdog)
  - [hardware/watchdog.h](https://sourcevu.sysprogs.com/rp2040/picosdk/files/src/rp2_common/hardware_watchdog/include/hardware/watchdog.h)
  - Ejemplo [pico-examples · clocks/watchdog](https://github.com/raspberrypi/pico-examples/tree/master/watchdog)

### 1.2 [P0] ✅ Credenciales Wi-Fi configurables desde la web (persistidas en Flash)

- **Qué:** hoy `WIFI_SSID`/`WIFI_PASSWORD` son macros de compilación en
  [`include/web_server.hpp`](https://github.com/AlessandroKlein/PicoReflex/blob/main/include/web_server.hpp). Hay que guardarlas en
  Flash (junto a `PersistentData`) y permitir editarlas desde el panel web
  (endpoint `/config`).
- **Cómo:** ampliar `PersistentData` con `ssid[]`/`password[]`, añadir campos al
  formulario HTML y usar esas credenciales en `cyw43_arch_wifi_connect_timeout_ms`.
  Reutilizar `flash_safe_execute()` ya implementado en `flash_storage.cpp`.
- **Por qué:** evita recompilar/flashear para cambiar de red, y convierte al
  dispositivo en configurable en campo (mismo criterio que el resto de la config).
- **Referencias:**
  - [hardware_flash](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash)
  - [pico_cyw43_arch](https://www.raspberrypi.com/documentation/pico-sdk/networking.html#pico_cyw43_arch)

### 1.3 [P0] ✅ Re-conexión Wi-Fi en caliente (manejo de caída del enlace)

- **Qué:** la conexión STA solo se intenta en `network_up()` (arranque y despertar
  de DORMANT). Si el AP se apaga o se pierde el enlace **durante** la operación,
  no hay re-intento: la web y el display siguen mostrando una IP que ya no sirve.
- **Cómo:** en el bucle del Core 1, consultar `cyw43_wifi_link_status(&cyw43_state,
  CYW43_ITF_STA)`; si es distinto de `CYW43_LINK_JOIN`, re-intentar conexión con
  *backoff*, o degradar a SoftAP tras N intentos (captive portal ya listo).
- **Por qué:** robustez de la conectividad "redundante" que ya promete el diseño
  (STA → AP), pero hoy solo funciona en el arranque.
- **Referencias:**
  - [cyw43_wifi_link_status / CYW43_LINK_JOIN](https://github.com/georgerobotics/cyw43-driver/issues/62)
  - [pico_cyw43_arch](https://www.raspberrypi.com/documentation/pico-sdk/networking.html#pico_cyw43_arch)

### 1.4 [P1] ✅ Hostname mDNS (`picoreflex.local`)

- **Qué:** publicar el nombre `picoreflex.local` para acceder al panel sin saber
  la IP (muy útil en modo STA, donde el DHCP asigna una IP variable).
- **Cómo:** habilitar `LWIP_MDNS_RESPONDER` en `lwipopts.h` y registrar el
  hostname con `mdns_resp_add_netif()` / `mdns_resp_add_service()`
  (`lwip/apps/mdns`).
- **Por qué:** mejora la experiencia de uso (abrir `http://picoreflex.local` en
  vez de leer la IP en el display).
- **Referencias:**
  - [lwIP mDNS responder](https://www.nongnu.org/lwip/2_1_x/group__mdns.html)
  - Ejemplo [pico-examples · pico_w/mdns](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi)

### 1.5 [P1] ✅ OTA (actualización de firmware por aire)

- **Qué:** permitir actualizar el `.uf2`/firmware desde la web, sin cable USB.
- **Cómo:** particionar la Flash en dos imágenes (A/B) + bootloader de arranque.
  Opciones estudiadas: [rp2-booster-bootloader](https://github.com/sidecartridge/rp2-booster-bootloader)
  (bootloader de segundo nivel para RP2040/Pico W) o
  [pico_fota_bootloader](https://github.com/JZimnol/pico_fota_bootloader).
  Requiere además publicar el binario por HTTP y verificar integridad (CRC/hash)
  antes de conmutar imagen.
- **Por qué:** evita el ciclo BOOTSEL+arrastrar `.uf2` en cada actualización;
  habilitaría distribución en campo.
- **Referencias:**
  - [pico_bootsel_via_double_reset (Pico SDK)](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_bootsel_via_double_reset)
  - [hardware_flash](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash)

### 1.6 [P1] ✅ Soporte de display SH1106

- **Qué:** el driver actual (`display_driver.cpp`) apunta a **SSD1306** (RAM
  128×64). Muchos OLED "128×64" económicos son en realidad **SH1106** (RAM
  132×64), que necesita un **desfase de 2 píxeles** en la columna inicial.
- **Cómo:** añadir una flag de controlador y, para SH1106, usar `column address`
  con *offset* +2 (p. ej. `0x21, 0x02, 0x7F` en vez de `0x21, 0x00, 0x7F`) y
  `0xDA, 0x12` → `0xDA, 0x02`. Detección automática es difícil por I2C (misma
  dirección); suele fijarse por configuración.
- **Por qué:** amplía la compatibilidad de hardware sin cambiar el resto del
  sistema.
- **Referencias:**
  - [SH1106 vs SSD1306](https://pcbsync.com/sh1106-vs-ssd1306/)
  - [oled-i2c (comparación de controladores)](https://docs.rs/oled-i2c/latest/src/oled_i2c/lib.rs.html)

### 1.7 [P1] ✅ Calibración de la curva de descarga de la batería

- **Qué:** `battery_monitor.cpp` usa una curva **fija** (4.2 V=100 %, 3.7 V=50 %,
  3.0 V=0 %) e ignora la caída de tensión por carga (el Wi-Fi drena corriente y
  el divisor VSYS/3 se lee bajo carga, lo que subestima el porcentaje).
- **Cómo:** añadir parámetros de calibración (pendiente/offset o tabla por
  tramos) configurables desde la web, y muestrear el ADC en reposo (radio en
  pausa) o con corrección por corriente estimada. Guardar la tabla en Flash.
- **Por qué:** mejora la exactitud del "% de batería" y de la alerta de batería
  baja, que hoy es solo orientativa.
- **Referencias:**
  - [hardware_adc](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc)

### 1.8 [P2] Más modos de juego

- **Qué:** añadir variantes como **modo duelo** (dos Pico W por Wi-Fi) o
  **reacción por niveles** (la velocidad entre estímulos aumenta con los
  aciertos). *(El modo **Simón Dice** ya está implementado en v0.6.0.)*
- **Cómo:** el motor ya está aislado en `game_logic.cpp` con un `enum GameMode`;
  agregar valores y ramas en la máquina de estados, más campos en `GameConfig`.
- **Por qué:** enriquece el juego sin tocar la infraestructura (red, display,
  persistencia).

### 1.9 [P2] Autonomía estimada en el panel web

- **Qué:** mostrar "autonomía estimada" (horas restantes) junto al % de batería,
  usando el consumo medido/estimado (100–120 mA en juego, < 1.5 mA en dormant).
- **Cómo:** derivar de `battery_pct` + capacidad configurada (mAh) en el estado
  compartido; presentarlo en `build_page()`.
- **Por qué:** hace tangible el dato de batería para el usuario.

---

## 2. Deuda técnica / robustez

### 2.1 [P1] ✅ Debounce por hardware (PIO) vs software

- **Situación:** el debounce es **software**, dentro de la IRQ GPIO
  (`BUTTON_DEBOUNCE_US = 5000 µs` en `pin_defs.hpp`, chequeo de `g_last_edge_us`
  en `game_logic.cpp`). Es simple pero consume CPU en la IRQ y no filtra todo
  tipo de rebote (p. ej. ruido corto).
- **Propuesta:** un programa **PIO** que muestree el pin y emita el evento de
  flanco ya "limpio", liberando a la CPU. Como alternativa, mantener el software
  pero mover el filtrado fuera de la IRQ (a una tarea/cola).
- **Referencias:**
  - [hardware_pio](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pio)
  - [PIO debounce (Raspberry Pi Forums)](https://forums.raspberrypi.com/viewtopic.php?t=399763)

### 2.2 [P0] ✅ Persistencia de la configuración de red en Flash

- **Situación:** Flash persiste solo récords/nombre/semilla (`flash_storage.cpp`).
  La config de red (SSID/password) es de compilación (ver 1.2).
- **Propuesta:** unificar la persistencia: ampliar `PersistentData` para incluir
  SSID/password y la tabla de calibración de batería, con `version` para migrar
  estructuras (ya existe el campo `version`).
- **Referencias:** [hardware_flash](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash)

### 2.3 [P0] ✅ Manejo de error de conexión Wi-Fi en caliente

- **Situación:** `network_up()` bloquea 10 s en `cyw43_arch_wifi_connect_timeout_ms`
  solo al arrancar; no hay re-intento posterior (ver 1.3).
- **Propuesta:** máquina de estados de red (conectando → conectado → caído →
  reintento → AP) en el Core 1, con *backoff* exponencial y degradación a AP.

### 2.4 [P1] Manejo de ausencia del display I2C

- **Situación:** `display::init()` retorna `false` si no hay ACK, pero
  `refresh_display()` lo intenta en cada ciclo (el escáner I2C con NACK es
  inofensivo, pero repetir `show()` sobre un bus sin dispositivo es ruido).
- **Propuesta:** registrar `g_ready == false` una sola vez y saltear `show()`
  permanentemente (o reintentar con *backoff*).

### 2.5 [P2] Reentrancia / concurrencia de `flash_safe_execute`

- **Situación:** `flash_safe_execute` ya bloquea al otro núcleo y deshabilita IRQs
  (correcto), pero si en el futuro ambos núcleos escriben Flash, hay que
  serializarlos explícitamente (hoy solo escribe el Core 0).
- **Propuesta:** una `critical_section`/`mutex` global para las escrituras a
  Flash, y documentar el contrato en `flash_storage`.

### 2.6 [P2] Tamaño de la imagen y memoria

- **Situación:** `text` ~356 KB / `bss` ~82 KB (versión 0.1.1). Queda margen, pero
  el búfer HTML (`g_body[8192]`, ampliado en v0.3.3) y el framebuffer OLED (1 KB)
  son estáticos.
- **Propuesta:** si se añade OTA (doble imagen), revisar el particionado de la
  Flash de 2 MB y el presupuesto de RAM de 264 KB.

---

## 3. Resumen por prioridad

| # | Mejora | Prioridad |
|---|---|---|
| 1.1 | Watchdog timer | P0 |
| 1.2 | Credenciales Wi-Fi desde la web | P0 |
| 1.3 | Re-conexión Wi-Fi en caliente | P0 |
| 2.2 | Persistencia de config de red en Flash | P0 |
| 2.3 | Manejo de error de conexión (estados de red) | P0 |
| 1.4 | mDNS `picoreflex.local` | P1 |
| 1.5 | OTA por aire | P1 |
| 1.6 | Soporte SH1106 | P1 |
| 1.7 | Calibración de batería | P1 |
| 2.1 | Debounce por PIO | P1 |
| 2.4 | Manejo de ausencia del display | P1 |
| 1.8 | Más modos de juego | P2 |
| 1.9 | Autonomía estimada en la web | P2 |
| 2.5 | Reentrancia de Flash | P2 |
| 2.6 | Tamaño de imagen / memoria | P2 |

---

## 4. Referencias principales

- [Índice Doxygen del Pico SDK](https://www.raspberrypi.com/documentation/pico-sdk/index_doxygen.html)
- [pico-examples (Raspberry Pi)](https://github.com/raspberrypi/pico-examples)
- [lwIP (mDNS, raw API)](https://www.nongnu.org/lwip/2_1_x/)
- [rp2-booster-bootloader (OTA)](https://github.com/sidecartridge/rp2-booster-bootloader)
- [pico_fota_bootloader (OTA)](https://github.com/JZimnol/pico_fota_bootloader)
