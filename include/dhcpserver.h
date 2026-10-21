/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018-2019 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file   dhcpserver.h
 * @brief  Servidor DHCP mínimo para el modo Access Point (código MIT de MicroPython).
 *
 * ¿POR QUÉ es necesario? El chip CYW43 de la Pico W NO trae servidor DHCP en modo
 * Access Point (la opción CYW43_NETUTILS está desactivada por defecto). Sin DHCP,
 * el teléfono se asocia al AP pero NUNCA recibe una dirección IP, por lo que
 * "conecta y se desconecta" (no puede hablar con 192.168.4.1).
 *
 * Este servidor responde el protocolo DHCP (RFC 2131) por UDP en el puerto 67 y
 * asigna IPs de la subred del AP (192.168.4.x) a cada cliente que se conecta.
 * Implementación basada en el ejemplo oficial pico-examples
 * (pico_w/wifi/access_point/dhcpserver), derivado de MicroPython.
 *
 * Protocolo resumido (DORA):
 *   Discover -> Offer -> Request -> Ack. Cada cliente recibe una IP fija por su
 *   MAC durante el tiempo de lease (24 h), para que no cambie entre reconexiones.
 */

#ifndef MICROPY_INCLUDED_LIB_NETUTILS_DHCPSERVER_H
#define MICROPY_INCLUDED_LIB_NETUTILS_DHCPSERVER_H

#include "lwip/ip_addr.h"

// IPs que reparte el servidor: 192.168.4.(DHCPS_BASE_IP .. DHCPS_BASE_IP + MAX - 1)
// Con BASE=16 y MAX=8 -> 192.168.4.16 .. 192.168.4.23 (la .1 es la propia Pico).
#define DHCPS_BASE_IP (16)   ///< Primer octeto de host a asignar.
#define DHCPS_MAX_IP (8)     ///< Cantidad máxima de clientes simultáneos.

// Arrendamiento ("lease") de una IP a un cliente, identificado por su MAC.
typedef struct _dhcp_server_lease_t {
    uint8_t mac[6];      ///< Dirección MAC del cliente (00:00:00:00:00:00 = libre).
    uint16_t expiry;     ///< Marca de tiempo de expiración (en ticks de 16 bits).
} dhcp_server_lease_t;

// Estado del servidor DHCP.
typedef struct _dhcp_server_t {
    ip_addr_t ip;                              ///< IP del servidor/AP (p. ej. 192.168.4.1).
    ip_addr_t nm;                              ///< Máscara de red (255.255.255.0).
    dhcp_server_lease_t lease[DHCPS_MAX_IP];   ///< Tabla de IPs arrendadas.
    struct udp_pcb *udp;                       ///< PCB UDP en el puerto 67.
} dhcp_server_t;

// Guardas extern "C": el .c se compila como C, pero se incluye desde C++ (web_server.cpp).
#ifdef __cplusplus
extern "C" {
#endif

void dhcp_server_init(dhcp_server_t *d, struct netif *nif, ip_addr_t *ip, ip_addr_t *nm);
void dhcp_server_deinit(dhcp_server_t *d);

#ifdef __cplusplus
}
#endif

#endif // MICROPY_INCLUDED_LIB_NETUTILS_DHCPSERVER_H
