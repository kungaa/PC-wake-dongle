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
#include "hardware/watchdog.h"

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
#define WIFI_CONNECT_TIMEOUT_MS  30000  // overall budget for one connect spell
#define WIFI_REJOIN_INTERVAL_MS   4000  // re-issue the async join this often while connecting
#define WIFI_RETRY_BACKOFF_MS     3000  // wait this long before re-trying after a failure
#define WIFI_LINK_LOSS_GRACE_MS   3000  // tolerate a transient link blip this long before reconnecting
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
// While CONNECTING: ms-since-boot of the last async-join we issued, so we can
// re-issue periodically (the CYW43 join probe often misses its slot when the
// radio is time-sliced with BLE and reports NONET -- the SDK's own connect loop
// re-joins on NONET rather than giving up; we mirror that).
static uint32_t last_join_ms = 0;
// While CONNECTED: ms-since-boot when the link first read non-UP, or 0 if the
// link is currently healthy. A single transient blip shouldn't tear us down, so
// we only reconnect once the link has stayed down past WIFI_LINK_LOSS_GRACE_MS.
static uint32_t link_lost_since_ms = 0;

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

// Issue an async join with the stored credentials. Open networks (no password
// stored) use CYW43_AUTH_OPEN. We use WPA2_MIXED rather than WPA2_AES: it
// accepts both TKIP and AES APs and is markedly less prone to the spurious
// BADAUTH-then-retry behaviour seen with the AES-only mode on first associate.
// Returns the driver result (0 == join request queued). Records the issue time
// so the CONNECTING state can pace re-joins; does NOT change wstate.
static int issue_join() {
    const Config_body &c = get_config();
    const uint32_t auth =
        c.wifi_pass[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN;
    const int rc = cyw43_arch_wifi_connect_async(
        c.wifi_ssid, c.wifi_pass[0] ? c.wifi_pass : nullptr, auth);
    last_join_ms = now_ms();
    return rc;
}

// Begin a fresh connect spell: issue the first join and enter CONNECTING.
static void start_connect() {
    const int rc = issue_join();
    enter(WST_CONNECTING);
    printf("[WiFi] connecting to \"%s\" (join rc=%d)\n", get_config().wifi_ssid, rc);
}

//--------------------------------------------------------------------+
// Lifecycle
//--------------------------------------------------------------------+

void wifi_init() {
    // Enable STA mode up front so a runtime toggle-on (wol_enabled flipped in
    // the web UI) can connect without a reboot. Idle STA mode does not
    // associate or scan on its own -- no airtime is used until start_connect().
    cyw43_arch_enable_sta_mode();
    // Disable Wi-Fi power-save. The SDK default (PM2) sleeps the radio between AP
    // beacons; on this chip the radio is already time-sliced with BLE, so that
    // extra sleep slows the association handshake and lets the link blip. This is
    // a permanently USB-powered dongle, so there is no reason to power-save Wi-Fi.
    // (Re-asserted on link-up below, where it reliably sticks.)
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
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
                link_lost_since_ms = 0;
                // Re-assert no-powersave now that the link is up: this is the
                // point it reliably sticks for the lifetime of the connection
                // (the early call in wifi_init can be a no-op before STA is up).
                cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
                enter(WST_CONNECTED);
                printf("[WiFi] connected, ip=%s\n",
                       ip4addr_ntoa(netif_ip4_addr(sta_netif())));
                return;
            }
            // Wrong password is the one genuinely terminal failure: re-joining
            // can't fix bad credentials, so back off and surface "auth failed".
            if (ls == CYW43_LINK_BADAUTH) {
                printf("[WiFi] bad auth -> backoff\n");
                cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
                enter(WST_BACKOFF);
                return;
            }
            // NONET/FAIL are NOT terminal here: on this dual-radio chip the join
            // probe frequently misses its slot (BLE has the radio) and reports
            // NONET even though the AP exists. The SDK's own blocking connect
            // re-issues the join on NONET rather than giving up; we do the same,
            // paced by WIFI_REJOIN_INTERVAL_MS, until the overall budget expires.
            if (since > WIFI_CONNECT_TIMEOUT_MS) {
                printf("[WiFi] connect budget expired (last link=%d) -> backoff\n", ls);
                cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
                enter(WST_BACKOFF);
                return;
            }
            if ((now_ms() - last_join_ms) >= WIFI_REJOIN_INTERVAL_MS) {
                const int rc = issue_join();
                printf("[WiFi] re-join (link=%d, rc=%d)\n", ls, rc);
            }
            return;
        }

        case WST_CONNECTED: {
            const int ls = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
            last_link_status = ls;
            if (ls == CYW43_LINK_UP) {
                link_lost_since_ms = 0; // healthy: clear any pending blip
                return;
            }
            // Link read non-UP. Tolerate a brief blip (a poll during a BLE
            // airtime slice can read down momentarily); only reconnect if it
            // stays down past the grace window.
            const uint32_t t = now_ms();
            if (link_lost_since_ms == 0) {
                link_lost_since_ms = t;
                printf("[WiFi] link blip (link=%d), watching\n", ls);
                return;
            }
            if ((t - link_lost_since_ms) >= WIFI_LINK_LOSS_GRACE_MS) {
                printf("[WiFi] link lost (link=%d) -> reconnect\n", ls);
                cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
                link_lost_since_ms = 0;
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

bool wifi_connecting() {
    return wstate == WST_CONNECTING;
}

// Like wifi_connected() but tolerant of a momentary link blip: it trusts the
// state machine (which holds WST_CONNECTED across short blips, see the grace
// window in wifi_task) rather than the instantaneous link read. Used by
// resolve, which otherwise loses clicks whenever BLE has the radio and the live
// link read dips to non-UP even though we are genuinely associated.
static bool wifi_associated() {
    return wstate == WST_CONNECTED;
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

// Bounded in-call ARP resolve. Total wall-clock budget is small enough to stay
// well under the host's HTTP/POST timeout (and the firmware watchdog, which the
// caller pets around config_save). We poll the cache, and on a miss issue one
// ARP query then keep servicing lwIP/cyw43 until the reply lands -- so a SINGLE
// "Resolve MAC" click usually succeeds instead of the user needing many tries.
#define WIFI_ARP_RESOLVE_BUDGET_MS 600
#define WIFI_ARP_POLL_STEP_MS       20

bool wifi_resolve_mac(const uint8_t ip[4], uint8_t out_mac[6]) {
    // Tolerate a momentary link blip (BLE airtime): trust the state machine, not
    // the instantaneous link read, so resolve doesn't lose clicks while associated.
    if (!wifi_associated()) return false;

    ip4_addr_t target;
    IP4_ADDR(&target, ip[0], ip[1], ip[2], ip[3]);

    const uint32_t deadline = now_ms() + WIFI_ARP_RESOLVE_BUDGET_MS;
    bool queried = false;

    for (;;) {
        struct eth_addr *eth = nullptr;
        const ip4_addr_t *found = nullptr;

        cyw43_arch_lwip_begin();
        const bool have = etharp_find_addr(sta_netif(), &target, &eth, &found) >= 0;
        if (have && eth) {
            memcpy(out_mac, eth->addr, 6);
            cyw43_arch_lwip_end();
            return true;
        }
        if (!queried) {
            // Not cached: issue exactly one ARP query, then wait for the reply
            // below rather than returning a miss immediately.
            etharp_query(sta_netif(), &target, nullptr);
            queried = true;
        }
        cyw43_arch_lwip_end();

        if (now_ms() >= deadline) return false;

        // Service the stack so the incoming ARP reply gets processed, then yield
        // briefly. cyw43_arch_poll() drives the (poll-mode) async context. Pet the
        // watchdog: this loop blocks the main loop's watchdog_update for up to the
        // resolve budget, which exceeds the 1 s watchdog window if left unfed.
#if !ENABLE_SERIAL
        watchdog_update();
#endif
        cyw43_arch_poll();
        sleep_ms(WIFI_ARP_POLL_STEP_MS);
    }
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
