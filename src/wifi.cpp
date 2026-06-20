//
// wifi.cpp -- Wi-Fi STA connection + Wake-on-LAN sender (the S5 wake path).
//
// Brought up only when Config_body.wol_enabled is set. The CYW43 chip runs BLE
// (BTstack) and Wi-Fi STA concurrently; lwIP (NO_SYS) is shared with the
// USB-NCM netif in usb_net.cpp. The STA netif is owned by the cyw43_arch driver
// (CYW43_LWIP=1) and reached via cyw43_state.netif[CYW43_ITF_STA].
//
// Everything here is serviced from the single main loop (wifi_task); the
// connect/retry logic is a small state machine so a missing AP or wrong
// password never blocks BLE scanning or the config web UI.
//

#include "wifi.h"

#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"

#include "config.h"

// STA interface index into cyw43_state.netif[].
#ifndef CYW43_ITF_STA
#define CYW43_ITF_STA 0
#endif

// Connect retry/backoff timings. We never give up: an AP may simply be down or
// out of range and come back later. Backoff keeps us off the air between tries.
#define WIFI_CONNECT_TIMEOUT_MS  15000  // give one async connect attempt this long
#define WIFI_RETRY_BACKOFF_MS     3000  // wait this long before re-trying after a failure
#define WIFI_SCAN_MAX            16      // SSIDs kept for the picker

typedef enum {
    WST_OFF,         // wol_enabled == 0: radio idle (BT only)
    WST_CONNECTING,  // async connect issued, waiting for link up + IP
    WST_CONNECTED,   // link up with an IP
    WST_BACKOFF,     // last attempt failed; waiting before retry
} wifi_state_t;

static wifi_state_t wstate = WST_OFF;
static uint32_t state_since_ms = 0;
static int last_link_status = CYW43_LINK_DOWN;
static char status_buf[40];

//--------------------------------------------------------------------+
// Helpers
//--------------------------------------------------------------------+

static struct netif *sta_netif() {
    return &cyw43_state.netif[CYW43_ITF_STA];
}

static uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

static void enter(wifi_state_t s) {
    wstate = s;
    state_since_ms = now_ms();
}

// Issue an async connect with the stored credentials. Open networks (no
// password stored) use CYW43_AUTH_OPEN. We use WPA2_MIXED rather than WPA2_AES:
// it accepts both TKIP and AES APs and is markedly less prone to the spurious
// BADAUTH-then-retry behaviour seen with the AES-only mode on first associate.
static void start_connect() {
    const Config_body &c = get_config();
    const uint32_t auth =
        c.wifi_pass[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN;
    cyw43_arch_wifi_connect_async(c.wifi_ssid,
                                  c.wifi_pass[0] ? c.wifi_pass : nullptr,
                                  auth);
    enter(WST_CONNECTING);
    printf("[WiFi] connecting to \"%s\"\n", c.wifi_ssid);
}

//--------------------------------------------------------------------+
// Lifecycle
//--------------------------------------------------------------------+

void wifi_init() {
    // Enable STA mode up front so a runtime toggle-on (wol_enabled flipped in
    // the web UI) can connect without a reboot. Idle STA mode does not
    // associate or scan on its own -- no airtime is used until start_connect().
    cyw43_arch_enable_sta_mode();
    const Config_body &c = get_config();
    if (c.wol_enabled && c.wifi_ssid[0]) {
        start_connect();
    } else {
        enter(WST_OFF);
    }
}

void wifi_task() {
    const Config_body &c = get_config();

    // wol_enabled toggled off (or SSID cleared) at runtime: drop back to idle.
    // cyw43_wifi_leave drives the driver's link-down callback, which stops the
    // DHCP client on the STA netif for us.
    if ((!c.wol_enabled || !c.wifi_ssid[0]) && wstate != WST_OFF) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        enter(WST_OFF);
        printf("[WiFi] disabled\n");
        return;
    }
    // wol_enabled toggled on while idle: begin connecting.
    if (c.wol_enabled && c.wifi_ssid[0] && wstate == WST_OFF) {
        start_connect();
        return;
    }

    const uint32_t since = now_ms() - state_since_ms;

    switch (wstate) {
        case WST_OFF:
            return;

        case WST_CONNECTING: {
            const int ls = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
            last_link_status = ls;
            if (ls == CYW43_LINK_UP) {
                enter(WST_CONNECTED);
                printf("[WiFi] connected, ip=%s\n",
                       ip4addr_ntoa(netif_ip4_addr(sta_netif())));
                return;
            }
            // Hard failures (bad auth / no net) or overall timeout -> back off.
            if (ls == CYW43_LINK_FAIL || ls == CYW43_LINK_BADAUTH ||
                ls == CYW43_LINK_NONET || since > WIFI_CONNECT_TIMEOUT_MS) {
                printf("[WiFi] connect failed (link=%d) -> backoff\n", ls);
                cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
                enter(WST_BACKOFF);
            }
            return;
        }

        case WST_CONNECTED: {
            const int ls = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
            last_link_status = ls;
            if (ls != CYW43_LINK_UP) {
                printf("[WiFi] link lost (link=%d) -> reconnect\n", ls);
                cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
                enter(WST_BACKOFF);
            }
            return;
        }

        case WST_BACKOFF:
            if (since >= WIFI_RETRY_BACKOFF_MS) start_connect();
            return;
    }
}

bool wifi_connected() {
    return wstate == WST_CONNECTED &&
           cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

const char *wifi_status_str() {
    switch (wstate) {
        case WST_OFF:
            snprintf(status_buf, sizeof(status_buf), "off");
            break;
        case WST_CONNECTING:
            snprintf(status_buf, sizeof(status_buf), "connecting");
            break;
        case WST_CONNECTED:
            snprintf(status_buf, sizeof(status_buf), "connected %s",
                     ip4addr_ntoa(netif_ip4_addr(sta_netif())));
            break;
        case WST_BACKOFF:
            if (last_link_status == CYW43_LINK_BADAUTH)
                snprintf(status_buf, sizeof(status_buf), "auth failed");
            else if (last_link_status == CYW43_LINK_NONET)
                snprintf(status_buf, sizeof(status_buf), "network not found");
            else
                snprintf(status_buf, sizeof(status_buf), "retrying");
            break;
    }
    return status_buf;
}

//--------------------------------------------------------------------+
// Wake-on-LAN
//--------------------------------------------------------------------+

bool wifi_send_wol(const uint8_t mac[6]) {
    if (!wifi_connected()) {
        printf("[WiFi] WOL skipped: not connected\n");
        return false;
    }

    // Magic packet: 6x 0xFF, then the target MAC repeated 16 times = 102 bytes.
    uint8_t magic[102];
    memset(magic, 0xFF, 6);
    for (int i = 0; i < 16; i++) memcpy(magic + 6 + i * 6, mac, 6);

    bool ok = false;
    cyw43_arch_lwip_begin();
    struct udp_pcb *pcb = udp_new();
    if (pcb) {
        // Bind to the STA netif so the broadcast leaves over Wi-Fi, not NCM, and
        // allow broadcast sends on this PCB.
        udp_bind_netif(pcb, sta_netif());
        ip_set_option(pcb, SOF_BROADCAST);

        struct netif *nif = sta_netif();
        // Subnet-directed broadcast (e.g. 192.168.17.255): host bits all 1.
        // This is what phone WOL apps use and is the most reliable way to reach
        // a sleeping NIC -- switches/APs flood it to every port on the subnet.
        ip_addr_t directed;
        ip4_addr_set_u32(ip_2_ip4(&directed),
                         (ip4_addr_get_u32(netif_ip4_addr(nif)) &
                          ip4_addr_get_u32(netif_ip4_netmask(nif))) |
                         ~ip4_addr_get_u32(netif_ip4_netmask(nif)));
        IP_SET_TYPE(&directed, IPADDR_TYPE_V4);

        // Send to BOTH UDP/9 and UDP/7 at the directed broadcast, and also the
        // limited broadcast, to cover every common WOL listener convention.
        const ip_addr_t *dests[2] = {&directed, IP_ADDR_BROADCAST};
        const u16_t ports[2] = {9, 7};
        for (int di = 0; di < 2; di++) {
            for (int pi = 0; pi < 2; pi++) {
                struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(magic), PBUF_RAM);
                if (!p) continue;
                memcpy(p->payload, magic, sizeof(magic));
                if (udp_sendto_if(pcb, p, dests[di], ports[pi], nif) == ERR_OK) ok = true;
                pbuf_free(p);
            }
        }
        udp_remove(pcb);
    }
    cyw43_arch_lwip_end();

    printf("[WiFi] WOL %s -> %02X:%02X:%02X:%02X:%02X:%02X\n",
           ok ? "sent" : "FAILED", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ok;
}

bool wifi_resolve_mac(const uint8_t ip[4], uint8_t out_mac[6]) {
    if (!wifi_connected()) return false;

    ip4_addr_t target;
    IP4_ADDR(&target, ip[0], ip[1], ip[2], ip[3]);

    struct eth_addr *eth = nullptr;
    const ip4_addr_t *found = nullptr;

    cyw43_arch_lwip_begin();
    const bool have = etharp_find_addr(sta_netif(), &target, &eth, &found) >= 0;
    if (have && eth) {
        memcpy(out_mac, eth->addr, 6);
    } else {
        // Not cached yet: kick an ARP query so a follow-up call can succeed.
        etharp_query(sta_netif(), &target, nullptr);
    }
    cyw43_arch_lwip_end();

    return have && eth;
}

//--------------------------------------------------------------------+
// AP scan for the SSID picker
//--------------------------------------------------------------------+

struct scan_entry { char ssid[33]; int16_t rssi; };
static scan_entry scan_list[WIFI_SCAN_MAX];
static int scan_count = 0;

static int scan_result_cb(void *, const cyw43_ev_scan_result_t *r) {
    if (!r->ssid_len) return 0; // hidden network
    char ssid[33];
    const int n = r->ssid_len < 32 ? r->ssid_len : 32;
    memcpy(ssid, r->ssid, n);
    ssid[n] = 0;

    // Dedup by SSID, keeping the strongest RSSI seen.
    for (int i = 0; i < scan_count; i++) {
        if (strcmp(scan_list[i].ssid, ssid) == 0) {
            if (r->rssi > scan_list[i].rssi) scan_list[i].rssi = r->rssi;
            return 0;
        }
    }
    if (scan_count < WIFI_SCAN_MAX) {
        memcpy(scan_list[scan_count].ssid, ssid, sizeof(ssid));
        scan_list[scan_count].rssi = r->rssi;
        scan_count++;
    }
    return 0;
}

static uint32_t last_scan_start_ms = 0;

void wifi_scan_start() {
    // STA mode is enabled unconditionally in wifi_init(), so the radio can scan
    // at any time -- it does NOT require an active connection (wstate). This is
    // what lets the user scan for an SSID right after ticking "Enable WOL",
    // before any SSID/password has been entered (the chicken-and-egg the old
    // wstate==WST_OFF guard wrongly blocked).
    //
    // The web UI polls this endpoint every ~1.5 s; debounce so we only launch a
    // fresh hardware scan every few seconds. Crucially we DON'T clear scan_list
    // between polls -- results accumulate (deduped) and stay visible, instead of
    // being wiped each poll (the bug that left the dropdown empty).
    if (cyw43_wifi_scan_active(&cyw43_state)) return;
    const uint32_t now = now_ms();
    if (last_scan_start_ms && (now - last_scan_start_ms) < 4000) return;
    last_scan_start_ms = now;

    cyw43_wifi_scan_options_t opts = {0};
    const int rc = cyw43_wifi_scan(&cyw43_state, &opts, nullptr, scan_result_cb);
    printf("[WiFi] scan start rc=%d (have %d)\n", rc, scan_count);
}

int wifi_scan_json(char *out, unsigned cap) {
    int n = snprintf(out, cap, "{\"scanning\":%d,\"networks\":[",
                     cyw43_wifi_scan_active(&cyw43_state) ? 1 : 0);
    for (int i = 0; i < scan_count && (unsigned) n < cap; i++) {
        // SSIDs can contain quotes/backslashes; escape minimally.
        char esc[65];
        int e = 0;
        for (const char *p = scan_list[i].ssid; *p && e < 62; p++) {
            if (*p == '"' || *p == '\\') esc[e++] = '\\';
            esc[e++] = *p;
        }
        esc[e] = 0;
        n += snprintf(out + n, cap - n, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                      i ? "," : "", esc, scan_list[i].rssi);
    }
    if ((unsigned) n < cap) n += snprintf(out + n, cap - n, "]}");
    return n;
}
