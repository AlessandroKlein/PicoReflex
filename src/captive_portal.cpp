/**
 * @file   captive_portal.cpp
 * @brief  Implementación del servidor DNS mínimo (captive portal).
 *
 * Responde a cualquier consulta DNS (tipo A) con la IP del dispositivo en modo
 * Access Point: 192.168.4.1. Así, el navegador del usuario es "capturado" y
 * redirigido a la página de configuración embebida.
 *
 * Estructura de la respuesta DNS (RFC 1035):
 *   - Header (12 B): TXID (eco de la consulta), flags 0x8180, QDCOUNT=1,
 *     ANCOUNT=1, NSCOUNT=0, ARCOUNT=0.
 *   - Question (eco íntegro de la consulta).
 *   - Answer (16 B): NAME (puntero 0xC00C), TYPE A (1), CLASS IN (1),
 *     TTL (60 s), RDLENGTH (4), RDATA (192.168.4.1).
 */

#include "captive_portal.hpp"

#include <cstring>

#include "lwip/udp.h"
#include "lwip/pbuf.h"

namespace captive {

// Puerto DNS estándar.
#define DNS_PORT 53u

// IP del Access Point (debe coincidir con AP_IP en web_server.cpp).
#define AP_IP_BYTES 192u, 168u, 4u, 1u

static struct udp_pcb* g_dns_pcb = nullptr;

// -----------------------------------------------------------------------------
// Callback de recepción UDP (consulta DNS entrante)
// -----------------------------------------------------------------------------
static void dns_recv_cb(void*, struct udp_pcb* pcb, struct pbuf* p,
                        const ip_addr_t* addr, u16_t port) {
    if (p == nullptr) return;
    if (p->tot_len < 12u) {   // Header DNS mínimo.
        pbuf_free(p);
        return;
    }

    // Copia la consulta a un buffer contiguo.
    uint8_t query[128];
    u16_t qlen = (p->tot_len < sizeof(query)) ? (u16_t)p->tot_len : (u16_t)sizeof(query);
    pbuf_copy_partial(p, query, qlen, 0);

    // Localiza el final de la sección "Question": QNAME (labels que terminan en
    // 0x00) + QTYPE (2) + QCLASS (2).
    int i = 12;
    while (i < qlen && query[i] != 0x00) {
        i += 1 + query[i];               // Salta longitud + contenido del label.
        if (i >= qlen) {                 // QNAME malformado.
            pbuf_free(p);
            return;
        }
    }
    int qend = i + 5;                    // 0x00 + QTYPE(2) + QCLASS(2).
    if (qend > qlen) {
        pbuf_free(p);
        return;
    }

    // QTYPE de la consulta (justo después del QNAME). Solo respondemos consultas
    // de tipo A (IPv4) con la IP del dispositivo; para AAAA (IPv6) y el resto se
    // devuelve NODATA (sin respuesta) para que el cliente reintente con A.
    uint16_t qtype = (uint16_t)((query[i + 1] << 8) | query[i + 2]);
    bool answer_a = (qtype == 0x0001);

    // Arma la respuesta.
    // Tamaño máximo: 12 (header) + (qend-12) (question) + 16 (answer) <= 148 B.
    // Se reserva 160 B para dejar margen y evitar desbordes de pila.
    uint8_t resp[160];
    int r = 0;

    // Header.
    resp[r++] = query[0];                // TXID (high).
    resp[r++] = query[1];                // TXID (low).
    resp[r++] = 0x81;                    // Flags: QR=1 (respuesta), RD eco.
    resp[r++] = 0x80;                    // RA=1 (recursion available).
    resp[r++] = 0x00; resp[r++] = 0x01;  // QDCOUNT = 1.
    resp[r++] = 0x00; resp[r++] = (answer_a ? 0x01 : 0x00);  // ANCOUNT.
    resp[r++] = 0x00; resp[r++] = 0x00;  // NSCOUNT = 0.
    resp[r++] = 0x00; resp[r++] = 0x00;  // ARCOUNT = 0.

    // Question: eco íntegro de la consulta.
    if (r + (qend - 12) > (int)sizeof(resp)) { pbuf_free(p); return; }
    std::memcpy(&resp[r], &query[12], (size_t)(qend - 12));
    r += qend - 12;

    // Answer (solo para consultas A).
    if (answer_a) {
        if (r + 16 > (int)sizeof(resp)) { pbuf_free(p); return; }
        resp[r++] = 0xC0; resp[r++] = 0x0C;  // NAME: puntero a offset 0x0C.
        resp[r++] = 0x00; resp[r++] = 0x01;  // TYPE = A.
        resp[r++] = 0x00; resp[r++] = 0x01;  // CLASS = IN.
        resp[r++] = 0x00; resp[r++] = 0x00;
        resp[r++] = 0x00; resp[r++] = 0x3C;  // TTL = 60 s.
        resp[r++] = 0x00; resp[r++] = 0x04;  // RDLENGTH = 4.
        resp[r++] = AP_IP_BYTES;             // RDATA = 192.168.4.1.
    }

    // Envía la respuesta al cliente.
    struct pbuf* out = pbuf_alloc(PBUF_TRANSPORT, (u16_t)r, PBUF_RAM);
    if (out) {
        std::memcpy(out->payload, resp, (size_t)r);
        udp_sendto(pcb, out, addr, port);
        pbuf_free(out);
    }

    pbuf_free(p);
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------
void init() {
    if (g_dns_pcb) return;               // Ya en marcha.

    g_dns_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!g_dns_pcb) return;

    // NO_SYS=1: se llama en el contexto del Core 1 (donde corre lwIP).
    if (udp_bind(g_dns_pcb, IP_ADDR_ANY, DNS_PORT) != ERR_OK) {
        udp_remove(g_dns_pcb);
        g_dns_pcb = nullptr;
        return;
    }
    udp_recv(g_dns_pcb, dns_recv_cb, nullptr);
}

void stop() {
    if (g_dns_pcb) {
        udp_remove(g_dns_pcb);
        g_dns_pcb = nullptr;
    }
}

} // namespace captive
