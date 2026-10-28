#ifndef _LWIPOPTS_EXAMPLE_COMMONH_H
#define _LWIPOPTS_EXAMPLE_COMMONH_H


// Common settings used in most of the pico_w examples
// (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html for details)

// allow override in some examples
// NO_SYS=0: lwIP usa la tarea tcpip de FreeRTOS (pico_lwip_freertos).
#ifndef NO_SYS
#define NO_SYS                      0
#endif
// allow override in some examples
#ifndef LWIP_SOCKET
#define LWIP_SOCKET                 0
#endif
#if PICO_CYW43_ARCH_POLL
#define MEM_LIBC_MALLOC             1
#else
// MEM_LIBC_MALLOC is incompatible with non polling versions
#define MEM_LIBC_MALLOC             0
#endif
#define MEM_ALIGNMENT               4
#ifndef MEM_SIZE
#define MEM_SIZE                    16000
#endif
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              32
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN                0
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0
// #define ETH_PAD_SIZE                2
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
// mDNS (hostname picoreflex.local). Requiere IGMP para el multicast IPv4
// (224.0.0.251), un slot de client-data por netif (el responder guarda ahí el
// struct mdns_host) y el módulo pico_lwip_mdns enlazado en CMakeLists.txt.
#define LWIP_IGMP                   1
#define LWIP_MDNS_RESPONDER         1
#define LWIP_NUM_NETIF_CLIENT_DATA  1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0
// El default de lwIP para MEMP_NUM_SYS_TIMEOUT es LWIP_NUM_SYS_TIMEOUT_INTERNAL+2
// (=9 con TCP+DHCP+DNS+IGMP+ARP). Esa fórmula NO cuenta el responder mDNS
// (sondeos/anuncios) ni el keepalive TCP de las conexiones HTTP que el navegador
// deja abiertas. En la renovación DHCP (~T1 = mitad del lease, ~3,5 min) los
// timeouts t1/t2/retransmisión se solapan con esos y agotan el pool -> PANIC
// "sys_timeout: pool MEMP_SYS_TIMEOUT is empty". 32 da margen sobrado
// (cada sys_timeo ~24 B; 32 = ~768 B de RAM estática, negligible).
#define MEMP_NUM_SYS_TIMEOUT        32

// Configuración REQUERIDA para NO_SYS=0 (pico_cyw43_arch_lwip_sys_freertos):
// lwIP corre en su tarea tcpip de FreeRTOS. Sin esto, lwIP usa los defaults 0
// de TCPIP_THREAD_STACKSIZE y TCPIP_MBOX_SIZE: xTaskCreate(stack 0) falla y la
// tarea tcpip jamás se crea, por lo que cyw43_arch_init() queda bloqueado para
// siempre en xSemaphoreTake(init_sem). Valores del ejemplo oficial pico-examples
// (pico_w/wifi/freertos/iperf/lwipopts.h).
#if !NO_SYS
// TCPIP_THREAD_STACKSIZE=2560 (no 1024): dhcp_server_process() corre en el hilo
// tcpip y declara dhcp_msg_t dhcp_msg (~548 B) + char hostname[32] + printf() +
// cadena udp_sendto_if()/pbuf_alloc(). Con 1024 B el stack del hilo tcpip se
// desborda y corrompe memoria -> reset por watchdog al asociar un cliente.
#define TCPIP_THREAD_STACKSIZE      2560
#define DEFAULT_THREAD_STACKSIZE    1024
#define TCPIP_MBOX_SIZE             8
#define DEFAULT_RAW_RECVMBOX_SIZE   8
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define DEFAULT_TCP_RECVMBOX_SIZE   8
#define DEFAULT_ACCEPTMBOX_SIZE     8
#endif

#ifndef NDEBUG
#define LWIP_DEBUG                  1
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          1
#endif

#define ETHARP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG                 LWIP_DBG_OFF
#define PBUF_DEBUG                  LWIP_DBG_OFF
#define API_LIB_DEBUG               LWIP_DBG_OFF
#define API_MSG_DEBUG               LWIP_DBG_OFF
#define SOCKETS_DEBUG               LWIP_DBG_OFF
#define ICMP_DEBUG                  LWIP_DBG_OFF
#define INET_DEBUG                  LWIP_DBG_OFF
#define IP_DEBUG                    LWIP_DBG_OFF
#define IP_REASS_DEBUG              LWIP_DBG_OFF
#define RAW_DEBUG                   LWIP_DBG_OFF
#define MEM_DEBUG                   LWIP_DBG_OFF
#define MEMP_DEBUG                  LWIP_DBG_OFF
#define SYS_DEBUG                   LWIP_DBG_OFF
#define TCP_DEBUG                   LWIP_DBG_OFF
#define TCP_INPUT_DEBUG             LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG            LWIP_DBG_OFF
#define TCP_RTO_DEBUG               LWIP_DBG_OFF
#define TCP_CWND_DEBUG              LWIP_DBG_OFF
#define TCP_WND_DEBUG               LWIP_DBG_OFF
#define TCP_FR_DEBUG                LWIP_DBG_OFF
#define TCP_QLEN_DEBUG              LWIP_DBG_OFF
#define TCP_RST_DEBUG               LWIP_DBG_OFF
#define UDP_DEBUG                   LWIP_DBG_OFF
#define TCPIP_DEBUG                 LWIP_DBG_OFF
#define PPP_DEBUG                   LWIP_DBG_OFF
#define SLIP_DEBUG                  LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_OFF

#endif /* __LWIPOPTS_H__ */
