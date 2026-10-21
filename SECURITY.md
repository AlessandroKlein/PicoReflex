# Política de seguridad

## Versiones soportadas

| Versión | Soportada          |
| ------- | ------------------ |
| 0.7.x   | :white_check_mark: |
| < 0.7   | :x:                |

## Reportar una vulnerabilidad

**No abras un issue o discusión pública.**

Enviá los detalles de forma privada a: **[aleklein23@gmail.com]**

Incluí:

- Descripción del problema.
- Pasos para reproducirlo o archivos afectados.
- Impacto estimado y mitigación sugerida (opcional).

## Qué esperar

- Acuse de recibo dentro de los **7 días**.
- Actualización de estado razonable mientras se investiga.
- Coordinación de un fix y de la publicación de un aviso cuando corresponda.

## Buenas prácticas de despliegue

- La contraseña del Access Point ya **no es fija**: se deriva del RUID de la Flash
  (única por placa) o se configura desde `/wifi` (8..15 caracteres) y se persiste.
  Las credenciales Wi-Fi (STA) también se configuran desde `/wifi`; las macros
  `WIFI_SSID_DEFAULT` / `WIFI_PASSWORD_DEFAULT` (`include/config.hpp`) son solo el
  valor inicial.
- El panel web ya exige **PIN** (`WEB_PIN`, por defecto `1234`, sobrescribible con
  `-DWEB_PIN=...`) y aplica **rate-limiting** en los endpoints de control (ver
  `docs/SEGURIDAD.md` §3.2 y §3.4).
- No expongas el dispositivo directamente a internet: está pensado para redes
  locales (SoftAP propio o Wi-Fi de confianza).
- Para entornos con terceros en la red, considerá además TLS en el panel web
  (mejora futura; ver `docs/SEGURIDAD.md` §3.5 y `docs/ROADMAP.md`).
