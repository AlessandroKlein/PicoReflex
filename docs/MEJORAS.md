# Mejoras Futuras — PicoReflex

> Hoja de ruta propuesta (resultado de la investigación). Ordenadas por
> valor/esfuerzo. No forman parte de la versión 0.1.x.

## Prioridad alta

1. **Credenciales Wi-Fi configurables vía web (STA)**
   - Hoy `WIFI_SSID`/`WIFI_PASSWORD` son macros en `web_server.hpp`.
   - Almacenar SSID/password en Flash (como el resto de la config) y permitir
     editarlos desde el panel web. Evita recompilar.
   - Referencia: `hardware_flash` + `pico_cyw43_arch`.

2. **Autenticación mínima del panel web**
   - PIN/token en la URL o campo de sesión, para que un tercero en el AP no
     controle el juego (ver `docs/SEGURIDAD.md` §2.2).

3. **Compatibilidad SH1106**
   - El display SH1106 (RAM 132×64) usa un desfase de 2 px de columna respecto
     del SSD1306. Añadir `#define` de controlador y ajustar `column address`.

## Prioridad media

4. **HTTPS (TLS) con mbedTLS**
   - La Pico W incluye `pico_mbedtls`. Permitiría cifrar el panel en modo STA.
   - Referencia: [pico_mbedtls](https://www.raspberrypi.com/documentation/pico-sdk/third_party.html).

5. **mDNS / hostname local**
   - Publicar `picoreflex.local` para no depender de recordar la IP (lwIP tiene
     soporte mDNS; ver `lwip/apps/mdns`).

6. **OTA (actualización por aire)**
   - Actualizar el firmware desde la web (doble imagen en Flash + bootloader).
   - Referencia: `pico_bootsel_via_double_reset`, particionado de Flash.

7. **Ranking persistente con más entradas y edición desde la web**
   - Hoy el ranking guarda 5 récords; ampliar a N y permitir ver/borrar desde la
     web.

## Prioridad baja / ideas

8. **Modo multijugador / duelo** (2 Pico W conectadas por Wi-Fi).
9. **Gráficos en el OLED** (barras, animación de "tiempo de reacción").
10. **Registro de eventos (DMA log) exportable** por USB/HTTP para análisis.
11. **Ajuste fino de la curva de descarga** de la batería (calibración por
    voltaje vs. porcentaje medidos).
12. **Perfil de consumo por componente** con mediciones reales para el informe.

## Investigación adicional sugerida

- Comparar `pico_rand` vs `xorshift32` en calidad estadística para secuencias
  largas (test diehard/TestU01) si se requieren competencias muy exigentes.
- Evaluar `pico_low_power` Pstate (RP2350) si se migra a Pico 2 W.
- Evaluar Bluetooth (CYW43439 tiene BT) como alternativa al Wi-Fi para
  configuración de corto alcance.
