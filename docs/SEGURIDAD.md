# Auditoría de Seguridad — PicoReflex

> **Alcance:** firmware C/C++ (Pico SDK 2.3.0) de un juego de reacción en Raspberry
> Pi Pico W, con servidor web lwIP (HTTP + DNS captive portal) y conectividad
> Wi-Fi STA/SoftAP. **Fecha:** 2026-10-20 · **Firmware auditado:** v0.4.4 (revisado)
>
> **Modelo de amenaza:** el dispositivo opera en una red local (SoftAP propio o
> Wi-Fi de confianza), no expuesto directamente a internet. Aun así se audita con
> criterio de defensa en profundidad, porque el SoftAP (y el modo STA en redes
> ajenas) amplía la superficie a terceros en la misma red o con acceso físico.

> **Cambios de estado desde v0.1.1 (auditoría original):**
> - ✅ XSS por `name` **resuelto** (`html_escape`) y la página `/player` **eliminada**.
> - ✅ `smin`/`smax` acotados y ordenados (clamp 50..60000 ms).
> - ✅ Credenciales STA ya no son solo macros: se configuran desde la web y se
>   persisten en Flash (siguen en texto plano, ver 3.3).
> - 🔄 AP volvió a **WPA2** (clave `12345678` por defecto, ver 3.4).

---

## 1. Resumen ejecutivo

| Severidad | Hallazgo | Ubicación |
|---|---|---|
| 🔴 Alta | Reflected/Stored XSS vía parámetro `name` sin escapar | `web_server.cpp:268` |
| 🟠 Media | Endpoints sin autenticación ni rate-limiting | `web_server.cpp:329-335` |
| 🟠 Media | Credenciales Wi-Fi (STA) compiladas en el firmware | `web_server.hpp:30,33` |
| 🟠 Media | Contraseña del AP por defecto `12345678` | `web_server.cpp:54` |
| 🟡 Baja | Sin TLS (HTTP plano) en el panel web | `web_server.cpp` |
| 🟡 Baja | `smin`/`smax` sin cota superior ni `smin ≤ smax` | `web_server.cpp:165-168` |
| 🟡 Baja | Flash sin cifrar + checksum no criptográfico | `flash_storage.cpp` |
| 🟢 Info | Debug de lwIP habilitado en builds sin `NDEBUG` | `lwipopts.h:57-61` |

**Lo positivo (ya correcto):** todos los buffers de parseo HTTP/DNS están
acotados, `strtol` (no `atoi`), `vsnprintf`/`snprintf` con límite, `static_assert`
de tamaño de `PersistentData`, y validación de rango en `difficulty`/`mode`. El
desborde de búfer del DNS fue corregido (búfer `resp[160]` + chequeo defensivo).

---

## 2. Superficie de ataque

| Componente | Protocolo | Riesgo principal |
|---|---|---|
| Servidor web | HTTP TCP :80 | Config/estadísticas sin cifrar, sin auth |
| Captive portal | DNS UDP :53 | Responder a consultas no confiables |
| Access Point | Wi-Fi WPA2 | Contraseña por defecto conocida |
| Flash (XIP) | Lectura física | Récords/nombre/semilla sin cifrar |
| USB stdio | CDC serial | `printf` de depuración legible por puerto |

---

## 3. Hallazgos detallados

### 3.1 ✅ Resuelto — XSS por campos de texto (nombre)

- **Estado:** **RESUELTO** en v0.1.3. Se agregó `html_escape()` (`web_ui.cpp`) para
  escapar `< > & " '` en los campos controlados por el usuario (nombre, SSID,
  contraseña). Además, la página `/player` (que exponía el campo `name`) fue
  **eliminada** en v0.4.0; el campo interno `user_name` ya no es editable por web.
- **Historial (v0.1.1):** `web_server.cpp` guardaba `name` en `cfg.user_name` y lo
  inyectaba en el HTML sin escapar, permitiendo XSS almacenado (CWE-79). El escape
  de salida y la eliminación de la ruta cierran el vector.

### 3.2 🟠 Endpoints sin autenticación ni rate-limiting

- **Código:** `web_server.cpp:329-335` — `/start`, `/stop`, `/reset` y `/config`
  ejecutan acciones (encolar eventos de control, cambiar configuración) sin
  verificar identidad ni limitar la frecuencia.
- **Impacto:** cualquiera en la red (AP o STA) puede reiniciar el juego, detener
  una ronda o dejar la configuración inservible (DoS lógico). No hay límite de
  conexiones simultáneas ni de peticiones/segundo.
- **Mitigación:** token/PIN (header `Authorization` o cookie) para los verbos de
  control, y un contador simple de peticiones por ventana de tiempo.

### 3.3 🟠 Credenciales Wi-Fi (STA) en texto plano (parcialmente resuelto)

- **Estado (v0.4.x):** ya **no** es necesario recompilar: el SSID/password se
  editan desde `/wifi` y se **persisten en Flash** (configurables desde el AP).
- **Código:** los *defaults* siguen siendo macros (`WIFI_SSID_DEFAULT` /
  `WIFI_PASSWORD_DEFAULT` en `include/config.hpp`) y las credenciales se guardan
  **en texto plano** en el sector Flash.
- **Impacto:** las credenciales de la red del usuario quedan en claro en la Flash,
  extraíbles con un volcado físico. CWE-798 / CWE-311 (sin cifrado).
- **Mitigación:** cifrar el campo en Flash (AES de `pico_mbedtls`) con clave
  derivada del RUID, o al menos evitar exponerlas en el binario.

### 3.4 🟠 Contraseña del Access Point por defecto `12345678`

- **Código:** `web_server.cpp` — `AP_PASSWORD "12345678"` fija (WPA2, canal 1).
  Entre v0.2.5 y v0.4.3 el AP estuvo **abierto** (sin clave) por un falso positivo
  de compatibilidad; en v0.4.4 volvió a **WPA2**.
- **Impacto:** cualquier persona conoce la clave del AP de respaldo; si el
  dispositivo queda en modo AP, entra sin autenticación. CWE-521 (contraseña
  débil/por defecto).
- **Mitigación:** derivar la clave de un identificador único (Flash RUID) o
  permitir cambiarla desde la web y persistirla.

### 3.5 🟡 HTTP sin TLS (texto plano)

- **Código:** el servidor usa la API raw TCP de lwIP en `HTTP_PORT 80`
  (`web_server.cpp:363-369`) sin capa TLS.
- **Impacto:** configuración (semilla, nombre, parámetros) y estadísticas viajan
  en claro; en modo STA un observador en la red puede leerlas o inyectarlas
  (MITM). CWE-311.
- **Mitigación:** `pico_mbedtls` (incluido en el SDK) con certificado
  autofirmado; aceptable aviso de seguridad en el navegador la primera vez.

### 3.6 🟡 Captive portal DNS — riesgos y mitigaciones

- **Código:** `captive_portal.cpp` — responde **a toda** consulta DNS en el
  puerto 53 con un registro A a `192.168.4.1`.
- **Análisis:**
  - **Parsing:** el bucle de QNAME está acotado por `qlen` (`captive_portal.cpp:52-59`)
    y la respuesta se protege con `if (qend + 16 > sizeof(resp))` (`:70`). ✅ sin
    desborde tras la corrección.
  - **Amplificación:** la respuesta (~40-50 B) no es mayor que la consulta, así
    que no es vector de amplificación DDoS. ✅
  - **Riesgo residual:** responde a consultas de **cualquier tipo** (no solo A) y
    a **cualquier cliente**; no filtra por puerto origen ni valida `QR`/`QTYPE`.
    Es aceptable para el captive portal, pero conviene responder solo a consultas
    con `QR=0` y `QTYPE=A`/`AAAA` y descartar el resto.
- **Mitigación:** validar `query[2]`/`query[3]` (flags) y `QTYPE` antes de
  responder; limitar el tamaño de QNAME eco.

### 3.7 🟡 Validación de entradas — `apply_config()` y `url_decode()`

Revisión punto por punto (`web_server.cpp:151-178`):

| Parámetro | Validación actual | Hallazgo |
|---|---|---|
| `seed` | Ninguna (cast a `uint32_t`) | Aceptable (cualquier u32 es válido). `strtol` devuelve `LONG`, así que valores > `INT_MAX` quedan limitados; no es un problema de seguridad. |
| `difficulty` | Rango `0..DIFF_COUNT` ✅ | Correcto. |
| `mode` | Rango `0..MODE_COUNT` ✅ | Correcto. |
| `smin` | Solo `> 0` | ⚠️ Sin cota superior. |
| `smax` | Solo `> 0 && >= smin` | ⚠️ Sin cota superior, y **no garantiza `smin ≤ smax`**. |
| `round` | Solo `> 0` | ⚠️ Sin cota superior (ronda "infinita" en modo TIMED). |
| `name` | `url_decode` acotado a 15 chars ✅ | ⚠️ **No se escapa HTML al mostrarlo** (ver 3.1). |

- **`smin`/`smax` — posible underflow:** si el usuario envía `smin=2000&smax=500`,
  `smin` queda en 2000 y `smax` **no se actualiza** (500 < 2000). En
  `game_logic.cpp` se calcula
  `span = stimulus_max_ms - stimulus_min_ms + 1` → resta **sin signo** que
  *wraps* a un valor enorme, y `delay_ms = smin + next_range(span)` produce un
  retardo arbitrario/errático (DoS lógico del juego, no corrupción de memoria).
  **Mitigación:** forzar `smax = max(smax, smin)` y acotar ambos a un rango
  sensato (p. ej. 50..10000 ms).
- **`url_decode()`** (`web_server.cpp:103-118`): correcto en cuanto a límites —
  comprueba `src[1]`/`src[2]` antes de leer, escribe como máximo `n-1` chars y
  termina con `\0`. ✅ No hay desborde. El problema está **aguas abajo** (falta de
  escape HTML), no en el decodificador.
- **`get_int_param()`** (`:89-93`): usa `strtol` (correcto, sin `atoi`). ✅
- **`find_param()`** (`:74-86`): no valida límite de longitud de la query, pero
  `query` siempre apunta dentro de `path[128]`, así que está acotado. ✅
- **`http_recv()`** (`:296-354`): `request[512]` y `path[128]` con copia acotada y
  `\0` final. ✅

### 3.8 🟢 Debug de lwIP y `printf` de depuración

- **Código:** `lwipopts.h:57-61`:

  ```c
  #ifndef NDEBUG
  #define LWIP_DEBUG            1
  #define LWIP_STATS            1
  #define LWIP_STATS_DISPLAY    1
  #endif
  ```

- **Impacto:** en builds sin `NDEBUG` (debug), lwIP emite trazas por stdio y
  `LWIP_STATS_DISPLAY` expone contadores internos; aumenta tamaño/uso de RAM y
  puede filtrar detalles internos por el puerto USB serial. En *Release* (que
  define `NDEBUG`) queda deshabilitado.
- **Recomendación:** asegurar que la entrega se compila en `Release` (build type),
  y no conectar el USB serial en entornos no confiables.

### 3.9 🟡 Datos en Flash sin cifrar (lectura física)

- **Código:** `flash_storage.cpp` — `PersistentData` (ranking, `user_name`,
  `last_seed`, `best_time_us`) se guarda en claro en el sector `0x1FF000`.
- **Checksum:** `update_checksum()` es una **suma simple de bytes**
  (`flash_storage.cpp:47-56`): detecta corrupción accidental pero **no** es un
  MAC criptográfico; quien tenga acceso de escritura a la Flash puede alterar los
  datos y recalcular la suma (CWE-345).
- **Impacto:** los datos (récords + nombre) son de **baja sensibilidad**; el
  riesgo real es la exposición de credenciales si en el futuro se persisten en
  Flash sin cifrar (ver 3.3).
- **Mitigación:** para datos sensibles futuros, cifrar (AES-GCM de `pico_mbedtls`)
  o al menos usar un hash con clave derivada del RUID de la Flash.

---

## 4. Recomendaciones priorizadas

### Esfuerzo bajo (rápidas)

1. **Escapar HTML de `user_name`** al renderizar (`web_server.cpp:268`). Cierra el XSS.
2. **Acotar y ordenar `smin`/`smax`/`round`** en `apply_config` (p. ej.
   `smin = clamp(smin, 50, 10000); smax = clamp(smax, smin, 10000);`).
3. **Responder solo a consultas DNS válidas** en el captive portal (validar `QR`
   y `QTYPE`).
4. **Compilar siempre en `Release`** (deja `NDEBUG`, apaga debug de lwIP).

### Esfuerzo medio

5. **Contraseña del AP configurable**, derivada del Flash RUID o editable por web
   y persistida; eliminar `12345678`.
6. **Autenticación mínima (PIN/token)** en los endpoints de control
   (`/start`, `/stop`, `/reset`, `/config`).
7. **Mover credenciales STA a Flash** (configurables desde el AP) en lugar de
   macros de compilación.

### Esfuerzo alto

8. **HTTPS con `pico_mbedtls`** para el panel web (certificado autofirmado o
   precargado). Ref: [mbedTLS en Pico SDK](https://www.raspberrypi.com/documentation/pico-sdk/third_party.html).
9. **Rate-limiting** y límite de conexiones concurrentes en el servidor lwIP.
10. **Cifrado/autenticación de los datos en Flash** (AES-GCM) si se almacenan
    datos sensibles; **secure boot / firmware firmado** si se comercializa.

---

## 5. Referencias

- [OWASP Internet of Things (IoT) Project / Top 10](https://owasp.org/www-project-internet-of-things/)
- [CWE-79 — Cross-site Scripting](https://cwe.mitre.org/data/definitions/79.html)
- [CWE-798 — Hard-coded Credentials](https://cwe.mitre.org/data/definitions/798.html)
- [CWE-521 — Weak Password Requirements](https://cwe.mitre.org/data/definitions/521.html)
- [CWE-311 — Missing Encryption of Sensitive Data](https://cwe.mitre.org/data/definitions/311.html)
- [mbedTLS (Pico SDK, third_party)](https://www.raspberrypi.com/documentation/pico-sdk/third_party.html)
- [lwIP TCP/UDP raw API](https://www.nongnu.org/lwip/2_1_x/)
