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
- **Método:** todos los endpoints usan `GET` (sin cuerpo).
- **Codificación:** los parámetros de texto van *URL-encoded* (espacio = `+` o `%20`).

## Endpoints

### Páginas de configuración

| Ruta | Contenido | Parámetros (query string) |
|---|---|---|
| `GET /` | Home: red, batería, resumen de stats y tabla de tiempos de reacción | — |
| `GET /seed` | Semilla del PRNG | `seed` |
| `GET /game` | Dificultad, modo, velocidad, duración de ronda, cuenta regresiva | `difficulty`, `mode`, `smin`, `smax`, `round`, `delay` |
| `GET /display` | Habilitar/deshabilitar el OLED | `oled` |
| `GET /wifi` | Credenciales Wi-Fi (SSID/password) | `ssid`, `pass` |
| `GET /player` | Nombre del jugador | `name` |

### Rutas de control (devuelven el Home)

| Ruta | Efecto |
|---|---|
| `GET /start` | Inicia la cuenta regresiva y luego la ronda (`START_ROUND`) |
| `GET /stop` | Detiene la ronda (`STOP_ROUND`) |
| `GET /reset` | Reinicia estadísticas (`RESET_STATS`) |

## Parámetros

| Parámetro | Ruta | Tipo | Rango / valores | Descripción |
|---|---|---|---|---|
| `seed` | `/seed` | entero | 0..2³²-1 (0 = aleatoria) | Semilla del PRNG |
| `difficulty` | `/game` | entero | 0=EASY, 1=NORMAL, 2=HARD | Dificultad (tolerancia 1000/500/250 ms) |
| `mode` | `/game` | entero | 0=FREE_PLAY, 1=TIMED, 2=SUDDEN_DEATH | Modo de juego |
| `smin` | `/game` | entero | 50..60000 ms | Intervalo mínimo entre estímulos |
| `smax` | `/game` | entero | ≥ `smin`, ≤ 60000 ms | Intervalo máximo entre estímulos |
| `round` | `/game` | entero | 1..3600 s | Duración de ronda (solo TIMED) |
| `delay` | `/game` | entero | 0..10 s | Cuenta regresiva antes de arrancar |
| `oled` | `/display` | entero | 0 = apagado, 1 = encendido | Habilita/deshabilita el OLED |
| `ssid` | `/wifi` | texto | ≤ 32 chars | SSID de la red Wi-Fi (STA) |
| `pass` | `/wifi` | texto | ≤ 64 chars | Contraseña de la red Wi-Fi (STA) |
| `name` | `/player` | texto | ≤ 15 chars | Nombre del jugador |

> Los valores numéricos fuera de rango se acotan automáticamente (`smin`/`smax` a
> 50..60000 ms con `smin ≤ smax`, `round` a 1..3600 s, `delay` a 0..10 s). Los
> `enum` (`difficulty`, `mode`) se ignoran si no corresponden a un valor válido.

### Ejemplos

```text
GET /seed?seed=12345
GET /game?difficulty=2&mode=1&smin=400&smax=900&round=30&delay=3
GET /display?oled=0
GET /wifi?ssid=MiRed&pass=clave123
GET /player?name=Leo
```

## Respuesta

- Código HTTP: `200 OK` (siempre, mientras el servidor esté activo).
- `Content-Type: text/html; charset=utf-8`.
- `Connection: close` (una conexión por petición).
- El cuerpo es la página HTML solicitada con los valores ya actualizados.
  Si la ruta no se reconoce, se devuelve el Home (`/`).

## Semántica de control (interna)

Los endpoints de control encolan eventos en la cola `queue_t` del estado
compartido (`control_send`), que el Core 0 consume en su bucle:

- `/start` → `ControlEvent::START_ROUND` (pasa por la cuenta regresiva)
- `/stop` → `ControlEvent::STOP_ROUND`
- `/reset` → `ControlEvent::RESET_STATS`

Las páginas de configuración aplican sus parámetros con `config_set()` y envían
`ControlEvent::CONFIG_CHANGED` para que el Core 0 persista en Flash. Si cambió el
Wi-Fi, se activa además `g_wifi_reconnect` para reconectar en el bucle del Core 1.

## Versionado / cambios de contrato

- **v0.2.0:** separación frontend/backend; rutas por página (`/seed`, `/game`,
  `/display`, `/wifi`, `/player`); nuevos parámetros `delay`, `oled`, `ssid`, `pass`.
- **v0.1.x:** ruta única `GET /config` (quedó **reemplazada** por las rutas por página).
- Cambios futuros se documentarán aquí y en `CHANGELOG.md` (SemVer).
