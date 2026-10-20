# Política de seguridad

## Versiones soportadas

| Versión | Soportada          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

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

- Cambiá la contraseña del Access Point (`AP_PASSWORD` en `src/web_server.cpp`) y
  las credenciales Wi-Fi (`WIFI_SSID` / `WIFI_PASSWORD` en `include/web_server.hpp`)
  antes de usar el dispositivo fuera del aula.
- No expongas el dispositivo directamente a internet: está pensado para redes
  locales (SoftAP propio o Wi-Fi de confianza).
- Para entornos con terceros en la red, habilitá autenticación del panel web y
  TLS (ver `docs/SEGURIDAD.md` y `docs/ROADMAP.md`).
