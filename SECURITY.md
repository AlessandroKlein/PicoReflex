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

- Cambiá los secretos por defecto (`JWT_SECRET`, `JWT_REFRESH_SECRET`) en producción.
- Usá contraseñas largas y aleatorias para el superadmin.
- Habilitá HTTPS y ajustá `CORS_ORIGIN` al dominio real.
- Revisá las cabeceras de seguridad (helmet) y el CSP según tu hosting.
