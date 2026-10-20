# Referencia de API HTTP — PicoReflex

> Documenta los endpoints del servidor web embebido (lwIP, puerto 80). Es una API
> de control local; ver `docs/SEGURIDAD.md` para el modelo de confianza.

## Base

- **Modo AP:** `http://192.168.4.1`
- **Modo STA:** la IP que asigna el DHCP (visible en el display).
- **Método:** todos los endpoints usan `GET` (sin cuerpo).

## Endpoints

| Método y ruta | Efecto | Parámetros (query string) |
|---|---|---|
| `GET /` | Página HTML de control + estadísticas | — |
| `GET /config` | Aplica configuración y devuelve la página | ver tabla de parámetros |
| `GET /start` | Inicia una ronda (`START_ROUND`) | — |
| `GET /stop` | Detiene la ronda (`STOP_ROUND`) | — |
| `GET /reset` | Reinicia estadísticas (`RESET_STATS`) | — |

## Parámetros de `GET /config`

| Parámetro | Tipo | Rango / valores | Descripción |
|---|---|---|---|
| `seed` | entero | 0..2³²-1 (0 = aleatoria) | Semilla del PRNG |
| `difficulty` | entero | 0=EASY, 1=NORMAL, 2=HARD | Dificultad (tolerancia 1000/500/250 ms) |
| `mode` | entero | 0=FREE_PLAY, 1=TIMED, 2=SUDDEN_DEATH | Modo de juego |
| `smin` | entero | > 0 | Intervalo mínimo entre estímulos (ms) |
| `smax` | entero | ≥ `smin` | Intervalo máximo entre estímulos (ms) |
| `round` | entero | > 0 | Duración de ronda (s), solo TIMED |
| `name` | texto | ≤ 15 chars | Nombre del jugador (URL-encoded) |

### Ejemplo

```text
GET /config?seed=12345&difficulty=2&mode=1&smin=400&smax=900&round=30&name=Leo
```

## Respuesta

- Código HTTP: `200 OK` (siempre, mientras el servidor esté activo).
- `Content-Type: text/html; charset=utf-8`.
- `Connection: close` (una conexión por petición).
- El cuerpo es la página HTML con los valores ya actualizados.

## Semántica de control (interna)

Los endpoints de control encolan eventos en la cola `queue_t` del estado
compartido (`control_send`), que el Core 0 consume en su bucle:

- `/start` → `ControlEvent::START_ROUND`
- `/stop` → `ControlEvent::STOP_ROUND`
- `/reset` → `ControlEvent::RESET_STATS`

## Versionado / cambios de contrato

- **v0.1.0:** endpoints y parámetros actuales (estables).
- Cambios futuros se documentarán aquí y en `CHANGELOG.md` (SemVer).
- No hay eliminación/deprecación planificada de endpoints.
