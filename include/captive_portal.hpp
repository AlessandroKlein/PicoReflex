/**
 * @file   captive_portal.hpp
 * @brief  Servidor DNS mínimo para "captive portal" (redirección en modo AP).
 *
 * Cuando la Pico W queda en modo Access Point (SoftAP), muchos dispositivos
 * (teléfonos/laptops) intentan validar la conexión resolviendo un nombre de
 * dominio (p. ej. connectivitycheck.gstatic.com). Este módulo responde a TODA
 * consulta DNS con la IP del dispositivo (192.168.4.1), de modo que cualquier
 * URL que abra el usuario termina mostrando la página de configuración.
 *
 * Implementación: API raw UDP de lwIP (NO_SYS=1). Se crea un PCB UDP en el
 * puerto 53 y se responde a cada query con un registro A que apunta a 192.168.4.1.
 *
 * Referencias:
 *  - lwIP raw UDP: https://www.nongnu.org/lwip/2_1_x/group__udp__raw.html
 *  - Formato de paquete DNS: RFC 1035.
 */

#ifndef PICO_REFLEX_CAPTIVE_PORTAL_HPP
#define PICO_REFLEX_CAPTIVE_PORTAL_HPP

namespace captive {

// Inicia el servidor DNS del captive portal (solo en modo AP). Idempotente.
void init();

// Detiene el servidor DNS (libera el PCB UDP). Se llama antes de apagar la red.
void stop();

} // namespace captive

#endif // PICO_REFLEX_CAPTIVE_PORTAL_HPP
