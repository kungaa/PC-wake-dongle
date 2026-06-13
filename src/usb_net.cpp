//
// usb_net.cpp -- CDC-NCM USB network interface + lwIP + config web UI.
//
// The dongle enumerates as a USB network adapter. A small lwIP stack runs
// over it (NO_SYS, serviced from the main loop):
//   - dhserver (TinyUSB lib/networking) hands the host an address
//   - mDNS responder answers http://picowake.local
//   - lwIP httpd serves the config page and a JSON API; all content is
//     generated in fs_open_custom / the POST hooks below (no static fsdata)
//
// The DHCP offer deliberately carries no gateway and no DNS server so the
// host never tries to route internet traffic or DNS lookups through us.
//

#include "usb_net.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tusb.h"

#include "dhserver.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/mdns.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

#include "hardware/watchdog.h"
#include "pico/time.h"
#include "pico/unique_id.h"

#include "ble.h"
#include "config.h"
#include "web_page.h"

// How long one /api/scan poll keeps the BLE scanner alive. The page polls
// every 2 s, so scanning stops ~15 s after the page is closed.
#define WEB_SCAN_KEEPALIVE_MS 15000

//--------------------------------------------------------------------+
// TinyUSB network glue (pattern from examples/device/net_lwip_webserver)
//--------------------------------------------------------------------+

// MAC the host's NIC uses; the device-side netif uses the same address with
// the last bit flipped (both locally administered, derived from the flash
// unique id in usb_net_init).
uint8_t tud_network_mac_address[6];

static struct netif netif_data;

#define INIT_IP4(a, b, c, d) {PP_HTONL(LWIP_MAKEU32(a, b, c, d))}

static const ip4_addr_t ipaddr  = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

static dhcp_entry_t dhcp_entries[] = {
    {{0}, INIT_IP4(192, 168, 7, 16), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 17), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 18), 24 * 60 * 60},
};

static const dhcp_config_t dhcp_config = {
    INIT_IP4(0, 0, 0, 0),          // router: none -- link-local only
    67,                            // listen port
    INIT_IP4(0, 0, 0, 0),          // dns: none -- never hijack host lookups
    nullptr,                       // domain
    sizeof(dhcp_entries) / sizeof(dhcp_entries[0]),
    dhcp_entries,
};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
    (void) netif;
    for (;;) {
        if (!tud_ready()) return ERR_USE;
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            return ERR_OK;
        }
        // transmit path busy: service USB until it frees up
        tud_task();
    }
}

static err_t netif_init_cb(struct netif *netif) {
    LWIP_ASSERT("netif != NULL", (netif != NULL));
    netif->mtu = CFG_TUD_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->state = NULL;
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->linkoutput = linkoutput_fn;
    netif->output = etharp_output;
    return ERR_OK;
}

// Process the frame inline (pattern from the TinyUSB 0.20 example): the NCM
// driver delivers one datagram at a time and recv_renew re-arms delivery.
extern "C" bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (size) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (!p) return false;
        pbuf_take(p, src, size);
        if (netif_data.input(p, &netif_data) != ERR_OK) {
            pbuf_free(p);
        }
        tud_network_recv_renew();
    }
    return true;
}

extern "C" uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    struct pbuf *p = (struct pbuf *) ref;
    (void) arg;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

extern "C" void tud_network_init_cb(void) {
    // frames are processed inline in tud_network_recv_cb; nothing to reset
}

//--------------------------------------------------------------------+
// HTTP content: / (page), /api/config, /api/scan -- via fs_open_custom
//--------------------------------------------------------------------+

// Build a complete response (headers + body) into a malloc'd buffer owned by
// the fs_file (freed in fs_close_custom).
static int make_file(struct fs_file *file, const char *status, const char *content_type,
                     const char *body, int body_len) {
    const int hdr_max = 160;
    char *buf = (char *) malloc(hdr_max + body_len);
    if (!buf) return 0;
    int hdr_len = snprintf(buf, hdr_max,
                           "HTTP/1.1 %s\r\nContent-Type: %s\r\nCache-Control: no-store\r\n"
                           "Connection: close\r\nContent-Length: %d\r\n\r\n",
                           status, content_type, body_len);
    memcpy(buf + hdr_len, body, body_len);
    memset(file, 0, sizeof(*file));
    file->data = buf; // malloc'd; reclaimed in fs_close_custom via file->data
    file->len = (int) (hdr_len + body_len);
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return 1;
}

// Device names go into JSON and then into the page DOM: strip anything that
// could break either (UTF-8 multibyte sequences pass through).
static void sanitize_name(const char *in, char *out, size_t cap) {
    size_t n = 0;
    for (; *in && n + 1 < cap; in++) {
        const unsigned char ch = (unsigned char) *in;
        out[n++] = (ch < 0x20 || strchr("\"\\<>&", ch)) ? '.' : (char) ch;
    }
    out[n] = 0;
}

static int json_config(char *out, size_t cap) {
    const Config_body &c = get_config();
    int n = snprintf(out, cap, "{\"enabled\":%d,\"led_off\":%d,\"version\":\"%s\",\"devices\":[",
                     c.ble_wake_enabled, c.led_off, PICO_PROGRAM_VERSION_STRING);
    for (int i = 0; i < c.device_count && (size_t) n < cap; i++) {
        const Wake_device &d = c.devices[i];
        char name[WAKE_NAME_LEN];
        sanitize_name(d.name, name, sizeof(name));
        n += snprintf(out + n, cap - n,
                      "%s{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"enabled\":%d,\"name\":\"%s\"}",
                      i ? "," : "",
                      d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5],
                      d.enabled, name);
    }
    if ((size_t) n < cap) n += snprintf(out + n, cap - n, "]}");
    return n;
}

static int json_scan(char *out, size_t cap) {
    ble_request_web_scan(WEB_SCAN_KEEPALIVE_MS);

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    int count;
    const ble_scan_result *r = ble_get_results(&count);

    int n = snprintf(out, cap, "{\"scanning\":%d,\"devices\":[", ble_scanning() ? 1 : 0);
    for (int i = 0; i < count && (size_t) n < cap; i++) {
        char name[sizeof(r[i].name)];
        sanitize_name(r[i].name, name, sizeof(name));
        n += snprintf(out + n, cap - n,
                      "%s{\"name\":\"%s\",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
                      "\"rssi\":%d,\"age\":%u}",
                      i ? "," : "", name,
                      r[i].mac[0], r[i].mac[1], r[i].mac[2], r[i].mac[3], r[i].mac[4], r[i].mac[5],
                      r[i].rssi, (unsigned) ((now - r[i].last_seen_ms) / 1000));
    }
    if ((size_t) n < cap) n += snprintf(out + n, cap - n, "]}");
    return n;
}

extern "C" int fs_open_custom(struct fs_file *file, const char *name) {
    if (strcmp(name, "/") == 0 || strcmp(name, "/index.html") == 0) {
        return make_file(file, "200 OK", "text/html; charset=utf-8",
                         WEB_PAGE, sizeof(WEB_PAGE) - 1);
    }
    if (strcmp(name, "/api/config") == 0) {
        static char body[1024];
        const int len = json_config(body, sizeof(body));
        return make_file(file, "200 OK", "application/json", body, len);
    }
    if (strcmp(name, "/api/scan") == 0) {
        static char body[2304];
        const int len = json_scan(body, sizeof(body));
        return make_file(file, "200 OK", "application/json", body, len);
    }
    if (strcmp(name, "/404.html") == 0) {
        static const char nf[] = "not found";
        return make_file(file, "404 Not Found", "text/plain", nf, sizeof(nf) - 1);
    }
    return 0;
}

extern "C" void fs_close_custom(struct fs_file *file) {
    if (file && file->data) {
        free(const_cast<char *>(file->data));
        file->data = NULL;
    }
}

extern "C" int fs_read_custom(struct fs_file *file, char *buffer, int count) {
    (void) file;
    (void) buffer;
    (void) count;
    return FS_READ_EOF; // all content is provided up front in fs_open_custom
}

//--------------------------------------------------------------------+
// POST /api/config -- full config replacement:
//   "enabled=1&dev=<MAC>|<0/1>|<label>&dev=..."  (each dev= URL-encoded)
//--------------------------------------------------------------------+

#define POST_BUFSIZE 1024
static char post_buf[POST_BUFSIZE];
static u16_t post_pos;
static void *post_conn;

static void url_decode(char *s) {
    char *out = s;
    for (; *s; s++) {
        if (*s == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], 0};
            *out++ = (char) strtol(hex, nullptr, 16);
            s += 2;
        } else {
            *out++ = (*s == '+') ? ' ' : *s;
        }
    }
    *out = 0;
}

static bool parse_mac(const char *s, uint8_t out[6]) {
    unsigned v[6];
    if (sscanf(s, "%2x%*1[:-]%2x%*1[:-]%2x%*1[:-]%2x%*1[:-]%2x%*1[:-]%2x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) out[i] = (uint8_t) v[i];
    return true;
}

static void apply_post(char *body) {
    Config_body &c = get_config();
    c.device_count = 0;

    // Tokens are split on '&' BEFORE url-decoding, so encoded '&' in labels
    // cannot break the framing. The page strips '|' from labels.
    for (char *tok = strtok(body, "&"); tok; tok = strtok(nullptr, "&")) {
        url_decode(tok);
        if (strncmp(tok, "enabled=", 8) == 0) {
            c.ble_wake_enabled = atoi(tok + 8) ? 1 : 0;
        } else if (strncmp(tok, "led_off=", 8) == 0) {
            c.led_off = atoi(tok + 8) ? 1 : 0;
        } else if (strncmp(tok, "dev=", 4) == 0 && c.device_count < WAKE_MAX_DEVICES) {
            // <MAC>|<0/1>|<label>
            char *mac_s = tok + 4;
            char *en_s = strchr(mac_s, '|');
            if (!en_s) continue;
            *en_s++ = 0;
            char *name_s = strchr(en_s, '|');
            if (!name_s) continue;
            *name_s++ = 0;

            Wake_device &d = c.devices[c.device_count];
            if (!parse_mac(mac_s, d.mac)) continue;
            d.enabled = atoi(en_s) ? 1 : 0;
            snprintf(d.name, sizeof(d.name), "%s", name_s);
            c.device_count++;
        }
    }

    // The sector erase blocks with interrupts off; feed the watchdog first.
    watchdog_update();
    config_save();
    printf("[NET] config saved: wake %s, %d device(s)\n",
           c.ble_wake_enabled ? "enabled" : "disabled", c.device_count);
}

extern "C" err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                                  u16_t http_request_len, int content_len, char *response_uri,
                                  u16_t response_uri_len, u8_t *post_auto_wnd) {
    (void) http_request;
    (void) http_request_len;
    (void) response_uri;
    (void) response_uri_len;
    (void) post_auto_wnd;
    if (strcmp(uri, "/api/config") != 0 || content_len >= POST_BUFSIZE) return ERR_VAL;
    if (post_conn) return ERR_USE; // one POST at a time
    post_conn = connection;
    post_pos = 0;
    return ERR_OK;
}

extern "C" err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    if (connection == post_conn && p) {
        const u16_t space = POST_BUFSIZE - 1 - post_pos;
        const u16_t take = p->tot_len < space ? p->tot_len : space;
        post_pos += pbuf_copy_partial(p, post_buf + post_pos, take, 0);
        post_buf[post_pos] = 0;
    }
    if (p) pbuf_free(p);
    return ERR_OK;
}

extern "C" void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    if (connection != post_conn) return;
    post_conn = nullptr;
    apply_post(post_buf);
    snprintf(response_uri, response_uri_len, "/api/config");
}

//--------------------------------------------------------------------+
// Init / service
//--------------------------------------------------------------------+

void usb_net_init() {
    // Stable locally-administered MAC derived from the flash unique id.
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    tud_network_mac_address[0] = 0x02;
    memcpy(tud_network_mac_address + 1, board_id.id + 3, 5);

    lwip_init();

    netif_data.hwaddr_len = 6;
    memcpy(netif_data.hwaddr, tud_network_mac_address, 6);
    netif_data.hwaddr[5] ^= 0x01; // device side must differ from host side

    netif_add(&netif_data, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ethernet_input);
#if LWIP_NETIF_HOSTNAME
    netif_set_hostname(&netif_data, "picowake");
#endif
    netif_set_default(&netif_data);
    netif_set_up(&netif_data);

    if (dhserv_init(&dhcp_config) != ERR_OK) {
        printf("[NET] dhcp server init failed\n");
    }
    mdns_resp_init();
    mdns_resp_add_netif(&netif_data, "picowake");
    httpd_init();

    printf("[NET] config UI at http://192.168.7.1/ (http://picowake.local/)\n");
}

void usb_net_task() {
    sys_check_timeouts();
}
