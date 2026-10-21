/**
 * @file   web_server.hpp
 * @brief  Servidor web embebido y gestión de conectividad Wi-Fi (tarea de red).
 *
 * Este módulo corre en la **tarea de red** (`network_task`) y se encarga de:
 *  - Inicializar el chip inalámbrico CYW43 (`cyw43_arch` en modo
 *    `sys_freertos`: lwIP en `NO_SYS=0` con su tarea `tcpip` propia).
 *  - Intentar conectarse como Station (STA) y, si falla en 10 s, conmutar a
 *    Access Point (SoftAP) con IP fija 192.168.4.1.
 *  - Servir una página web (HTTP sobre lwIP, API raw) para configurar el juego
 *    y ver estadísticas en tiempo real.
 *  - Actualizar el display OLED (SSD1306/SH1106).
 *
 * Referencias:
 *  - pico_cyw43_arch (STA/AP, LED onboard):
 *    https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#pico_cyw43_arch
 *  - lwIP raw API (tcp_new/tcp_bind/tcp_listen/tcp_accept/tcp_recv):
 *    https://www.nongnu.org/lwip/2_1_x/group__tcp__raw.html
 */

#ifndef PICO_REFLEX_WEB_SERVER_HPP
#define PICO_REFLEX_WEB_SERVER_HPP

namespace web {

// Inicializa cyw43_arch (sys_freertos) y el hardware. Debe llamarse
// UNA vez desde la tarea de red (network_task) antes de run().
void init();

// Bucle principal de la tarea de red (bloquea para siempre):
//   gestiona STA/AP, sirve HTTP y refresca el display.
void run();

} // namespace web

#endif // PICO_REFLEX_WEB_SERVER_HPP
