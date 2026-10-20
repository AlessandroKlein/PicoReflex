# Auditoría de Seguridad — PicoReflex

> Revisión de seguridad del firmware y de los servicios de red (HTTP + DNS).
> Contexto: el dispositivo opera en una red de acceso local (SoftAP propio o Wi-Fi
> de confianza), **no** expuesto directamente a internet. Aun así se aplican
> prácticas de codificación defensiva.

## 1. Superficie de ataque

| Componente | Protocolo | Riesgo principal |
|---|---|---|
| Servidor web | HTTP (puerto 80) | Configuración sin autenticar, sin cifrado |
| Captive portal | DNS UDP (puerto 53) | Parsing de paquetes no confiables |
| Access Point | Wi-Fi WPA2 | Contraseña por defecto fija |

## 2. Hallazgos y estado

### 2.1 ✅ Corregido — Desborde de búfer en la respuesta DNS

- **Archivo:** `src/captive_portal.cpp`.
- **Problema:** el búfer de respuesta `resp[128]` podía desbordarse. El tamaño
  máximo real de la respuesta es `12 (header) + 116 (question) + 16 (answer) =
  144` bytes; un atacante enviando una consulta DNS de 128 bytes con un QNAME
  largo podía escribir hasta ~148 bytes.
- **Solución:** se amplió el búfer a `resp[160]` y se añadió una comprobación
  defensiva `if (qend + 16 > sizeof(resp)) return;` antes de copiar.

### 2.2 ⚠️ Aceptado — Sin autenticación en la interfaz web

- **Situación:** `GET /start`, `/stop`, `/reset` y `/config` no requieren
  credenciales. Cualquier dispositivo conectado al AP puede controlar el juego.
- **Evaluación:** aceptable para el alcance del TP (red local de confianza). Se
  documenta como mejora futura (PIN/token en la URL o sesión).

### 2.3 ⚠️ Aceptado — HTTP sin cifrado (sin TLS)

- **Situación:** la página y los parámetros viajan en claro. En modo STA, un
  observador en la red podría leer la configuración.
- **Evaluación:** aceptable para el alcance. Mejora futura: HTTPS con mbedTLS
  (la Pico W incluye `pico_mbedtls`).

### 2.4 ⚠️ Aceptado — Contraseña de AP por defecto fija

- **Situación:** `PicoW-ReactionGame` / `12345678` está compilada en el código.
- **Evaluación:** aceptable como respaldo de configuración. Mejora futura:
  contraseña configurable vía web y persistida en Flash.

### 2.5 ✅ Correcto — Validación de entrada en el parser HTTP

- `get_int_param()` usa `strtol` (sin `atoi`), y los valores se acotan a los
  rangos válidos de los `enum` (modo, dificultad) antes de usarse.
- `url_decode()` respeta el tamaño del búfer (`i < n - 1`).
- `request[512]`, `path[128]` y `g_body[2048]` se llenan con tamaños acotados y
  `snprintf`/Builder con límite.

### 2.6 ✅ Correcto — Sin inyección de comandos / path traversal

- Las rutas se comparan con literales fijos (`/start`, `/stop`, `/reset`); no hay
  acceso a sistema de archivos ni ejecución de comandos.

## 3. Recomendaciones para producción (fuera del alcance del TP)

1. Autenticación mínima (PIN) en el panel web.
2. HTTPS (mbedTLS) para el panel en modo STA.
3. Contraseña de AP aleatoria/configurable persistida en Flash.
4. Límite de tasa (rate limiting) en el servidor HTTP.
5. Firmado/verificación de firmware (secure boot) si se distribuye comercialmente.

## 4. Notas de hardening ya aplicadas en el código

- **Buffers acotados** en todos los parsers (HTTP y DNS).
- **`snprintf` / Builder con límite** para toda salida formateada.
- **Estructuras con `static_assert`** (`PersistentData ≤ FLASH_PAGE_SIZE`).
- **Checksum** en los datos persistentes de Flash (detecta corrupción).
