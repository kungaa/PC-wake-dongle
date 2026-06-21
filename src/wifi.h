//
// wifi.h -- Wi-Fi STA connection + Wake-on-LAN sender (the S5 wake path).
//
// Brought up only when Config_body.wol_enabled is set. Joins the user's home
// Wi-Fi, gets a LAN address by DHCP, and can send a WOL magic packet to the
// configured target so a board that honours NIC-WOL-from-S5 still wakes even
// when USB-HID remote-wakeup (the F15 path) does not.
//

#ifndef WAKE_DONGLE_WIFI_H
#define WAKE_DONGLE_WIFI_H

#include <cstdint>

// Call once, after cyw43_arch_init(). Enables STA mode and (if wol_enabled)
// kicks off an async connect with the stored credentials. No-op for the radio
// if wol_enabled is 0 -- the chip stays effectively BT-only.
void wifi_init();

// Service the connection state machine; call every main-loop iteration.
void wifi_task();

// True once the STA link is up AND a DHCP lease has been obtained.
bool wifi_connected();

// True while a connect spell is in progress (association/DHCP not yet complete).
// Used to pause the config page's ACTIVE BLE scan during this window: active
// scanning transmits probe requests that steal airtime from Wi-Fi association on
// the single shared CYW43 radio, which is the main cause of slow/flaky joins.
// The wake path's PASSIVE scan is never affected by this.
bool wifi_connecting();

// Human-readable status for the web UI, e.g. "off", "connecting",
// "connected 192.168.1.77", "auth failed", "no network". Returns a pointer to
// a static buffer (valid until the next call).
const char *wifi_status_str();

// Send a Wake-on-LAN magic packet for `mac` (6 bytes) as a UDP broadcast on the
// Wi-Fi netif. No-op (returns false) if Wi-Fi isn't connected. Safe to call
// from the wake path.
bool wifi_send_wol(const uint8_t mac[6]);

// Best-effort ARP resolve: look up the MAC for `ip` (4 octets) on the Wi-Fi
// netif. The target must be reachable/awake on the LAN. On success copies 6
// bytes into out_mac and returns true; otherwise issues an ARP query (so a
// follow-up call may succeed) and returns false. Bounded, non-blocking-ish.
bool wifi_resolve_mac(const uint8_t ip[4], uint8_t out_mac[6]);

// Kick off / read a Wi-Fi AP scan for the SSID picker. wifi_scan_start()
// begins an async scan (no-op if one is already running). wifi_scan_json()
// writes a JSON array of {"ssid","rssi"} (deduped, strongest first) into out
// and returns bytes written.
void wifi_scan_start();
int  wifi_scan_json(char *out, unsigned cap);

#endif // WAKE_DONGLE_WIFI_H
