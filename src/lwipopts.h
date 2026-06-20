#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// lwIP runs NO_SYS over the TinyUSB NCM interface; everything is serviced
// from the single main loop (usb_net_task), so no OS/locking is needed.
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    8000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_UDP_PCB            8   // +DHCP client (Wi-Fi STA) and the WOL sender
#define PBUF_POOL_SIZE              8

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_UDP                    1
// DHCP *client* (port 68): used by the Wi-Fi STA netif to get a LAN address for
// the Wake-on-LAN path. The NCM netif is statically configured (no dhcp_start
// on it), and the NCM-side DHCP *server* is a separate raw-UDP impl (dhserver.c,
// port 67) -- the two coexist.
#define LWIP_DHCP                   1
#define LWIP_DNS                    0   // deliberately no DNS: never hijack host lookups
#define LWIP_IGMP                   1   // mDNS joins a multicast group

// Let ip4_input accept link-layer-addressed packets (src 0.0.0.0) destined
// for UDP port 67: required for the DHCP *server* to see client DISCOVERs.
#define LWIP_IP_ACCEPT_UDP_PORT(p) ((p) == PP_NTOHS(67))

#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_TCP_KEEPALIVE          1

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1  // required by the mDNS responder
#define LWIP_NUM_NETIF_CLIENT_DATA  1     // mDNS netif client data slot

#define LWIP_MDNS_RESPONDER         1     // answers http://picowake.local
#define MDNS_MAX_SERVICES           1

// HTTP server: all content is generated in fs_open_custom / the POST hooks
// (usb_net.cpp); the static fsdata table is empty (pico_fsdata.inc).
#define LWIP_HTTPD_CUSTOM_FILES     1
#define LWIP_HTTPD_DYNAMIC_HEADERS  0     // responses carry their own headers
#define LWIP_HTTPD_SUPPORT_POST     1
#define LWIP_HTTPD_SSI              0
#define LWIP_HTTPD_CGI              0
#define HTTPD_FSDATA_FILE           "pico_fsdata.inc"

#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

#define LWIP_CHKSUM_ALGORITHM       3

#endif /* LWIPOPTS_H */
