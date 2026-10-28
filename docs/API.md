# Referencia de API HTTP — PicoReflex

> Documenta los endpoints del servidor web embebido (lwIP, puerto 80). Es una API
> de control local; ver `docs/SEGURIDAD.md` para el modelo de confianza.
>
> En v0.2.0 el frontend (generación de HTML) está separado del backend (ruteo y
> parseo): el backend en `src/web_server.cpp` y la presentación en
> `src/web_ui.cpp` (`web_ui::render`).

## Base

- **Modo AP:** `http://192.168.4.1`
- **Modo STA:** la IP que asigna el DHCP (visible en el display).
- **Método:** `GET` para páginas/control; `POST` para la carga OTA (`/ota/upload`).
- **Autenticación (v0.7.0):** los endpoints de control/configuración exigen PIN
  (cookie `pr_auth` o cabecera `Authorization: Bearer <pin>`). Ver `docs/SEGURIDAD.md`.
- **Codificación:** los parámetros de texto van *URL-encoded* (espacio = `+` o `%20`).

## Endpoints

### Páginas de configuración

| Ruta | Contenido | Parámetros (query string) |
|---|---|---|
| `GET /` | Home: red, batería, resumen de stats y tabla de tiempos de reacción | — |
| `GET /seed` | Semilla del PRNG | `seed` |
| `GET /game` | Dificultad, modo, velocidad, duración de ronda, cuenta regresiva | `difficulty`, `mode`, `smin`, `smax`, `round`, `delay` |
| `GET /display` | Habilitar/deshabilitar el OLED y elegir controlador | `oled`, `controller` |
| `GET /wifi` | Credenciales Wi-Fi (SSID/password) + clave del AP | `ssid`, `pass`, `ap_pass` |
| `GET /battery` | Calibración de la curva de descarga | `vfull`, `vmid`, `vempty`, `loadcomp` |
| `GET /ota` | Página de actualización por aire (subir firmware) | — |

### Rutas de control (devuelven el Home)

| Ruta | Efecto |
|---|---|
| `GET /start` | Inicia la cuenta regresiva y luego la ronda (`START_ROUND`) |
| `GET /stop` | Detiene la ronda (`STOP_ROUND`) |
| `GET /reset` | Reinicia estadísticas (`RESET_STATS`) |
| `POST /ota/upload` | Recibe el binario de aplicación (CRC32 en `X-CRC32`) para OTA A/B |

> **Rutas retiradas/reservadas:**
> - `GET /player` — **eliminada** en v0.4.0 (no había modo multijugador; el campo
>   interno `user_name` queda con el valor por defecto "JUGADOR").
> - `GET /scan` — **reservada/interna** (v0.4.0): devolvía un array JSON de SSIDs
>   detectados, pero el botón "Buscar redes" del frontend se **eliminó** en v0.4.3
>   porque el escaneo del CYW43 no era fiable. El endpoint sigue en el backend sin
>   uso visible (solo documentado, no expuesto en la interfaz).

## Parámetros

| Parámetro | Ruta | Tipo | Rango / valores | Descripción |
|---|---|---|---|---|
| `seed` | `/seed` | entero | 0..2³²-1 (0 = aleatoria) | Semilla del PRNG |
| `difficulty` | `/game` | entero | 0=EASY, 1=NORMAL, 2=HARD | Dificultad (tolerancia 1000/500/250 ms) |
| `mode` | `/game` | entero | 0=FREE_PLAY, 1=TIMED, 2=SUDDEN_DEATH, 3=SIMON_SAYS | Modo de juego |
| `smin` | `/game` | entero | 50..60000 ms | Intervalo mínimo entre estímulos |
| `smax` | `/game` | entero | ≥ `smin`, ≤ 60000 ms | Intervalo máximo entre estímulos |
| `round` | `/game` | entero | 1..3600 s | Duración de ronda (solo TIMED) |
| `delay` | `/game` | entero | 0..10 s | Cuenta regresiva antes de arrancar |
| `oled` | `/display` | entero | 0 = apagado, 1 = encendido | Habilita/deshabilita el OLED |
| `controller` | `/display` | entero | 0 = SSD1306 (0.96"), 1 = SH1106 (1.3") | Controlador de display (se persiste en Flash) |
| `ssid` | `/wifi` | texto | ≤ 32 chars | SSID de la red Wi-Fi (STA) |
| `pass` | `/wifi` | texto | ≤ 64 chars | Contraseña de la red Wi-Fi (STA) |
| `ap_pass` | `/wifi` | texto | 8..15 chars (vacío = `12345678` por defecto) | Clave del AP de respaldo (WPA2) |
| `vfull` | `/battery` | entero | mV | Tensión de batería al 100 % (def. 4200) |
| `vmid` | `/battery` | entero | mV | Tensión al 50 % (def. 3700) |
| `vempty` | `/battery` | entero | mV | Tensión al 0 % (def. 3000) |
| `loadcomp` | `/battery` | entero | mV | Compensación por carga sumada a la lectura (def. 0) |

> Los valores numéricos fuera de rango se acotan automáticamente (`smin`/`smax` a
> 50..60000 ms con `smin ≤ smax`, `round` a 1..3600 s, `delay` a 0..10 s). Los
> `enum` (`difficulty`, `mode`) se ignoran si no corresponden a un valor válido.

### Ejemplos

```text
GET /seed?seed=12345
GET /game?difficulty=2&mode=1&smin=400&smax=900&round=30&delay=3
GET /display?oled=0
GET /display?controller=1
GET /wifi?ssid=MiRed&pass=clave123
```

## Respuesta

- Código HTTP: `200 OK` (siempre, mientras el servidor esté activo).
- `Content-Type: text/html; charset=utf-8`.
- `Connection: close` (una conexión por petición).
- El cuerpo es la página HTML solicitada con los valores ya actualizados.
  Si la ruta no se reconoce, se devuelve el Home (`/`).

## Semántica de control (interna)

Los endpoints de control encolan eventos en la cola `queue_t` del estado
compartido (`control_send`), que la tarea de juego (`game_task`) consume en su bucle:

- `/start` → `ControlEvent::START_ROUND` (pasa por la cuenta regresiva)
- `/stop` → `ControlEvent::STOP_ROUND`
- `/reset` → `ControlEvent::RESET_STATS`

Las páginas de configuración aplican sus parámetros con `config_set()` y envían
`ControlEvent::CONFIG_CHANGED` para que la tarea de juego persista en Flash. Si cambió el
Wi-Fi, se activa además `g_wifi_reconnect` para reconectar en el bucle de la tarea de red.

## Versionado / cambios de contrato

- **v0.7.0:** autenticación PIN (cookie/Bearer) + rate-limiting; nuevos endpoints
  `/battery` (calibración) y `POST /ota/upload` + página `/ota` (OTA A/B);
  parámetros `ap_pass` en `/wifi` y `controller` en `/display` (SSD1306/SH1106).
- **v0.6.0:** modo `SIMON_SAYS` (`mode=3`) añadido.
- **v0.4.0:** eliminada `GET /player`; ruta `GET /scan` (SSIDs en JSON) reservada.
- **v0.4.3:** frontend de `/scan` (botón "Buscar redes") eliminado; `/scan` queda
  como endpoint interno sin uso visible.
- **v0.2.0:** separación frontend/backend; rutas por página (`/seed`, `/game`,
  `/display`, `/wifi`, `/player`); nuevos parámetros `delay`, `oled`, `ssid`, `pass`.
- **v0.1.x:** ruta única `GET /config` (quedó **reemplazada** por las rutas por página).
- Cambios futuros se documentarán aquí y en `CHANGELOG.md` (SemVer).
