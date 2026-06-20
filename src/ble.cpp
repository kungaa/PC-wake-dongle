//
// ble.cpp -- passive BLE advertisement scanning.
//
// Two consumers:
//   1. Wake: while the host is suspended (or never enumerated, e.g. PC in
//      soft-off with powered USB), scan for the configured target MAC and
//      trigger the wake FSM when it advertises.
//   2. Config web UI: while the UI polls /api/scan, keep a table of every
//      advertiser seen (MAC, RSSI, name) so the user can pick a device.
//

#include "ble.h"

#include <cstdio>
#include <cstring>

#include "ad_parser.h"
#include "bluetooth_data_types.h"
#include "btstack_event.h"
#include "gap.h"
#include "hci.h"
#include "pico/time.h"
#include "tusb.h"

#include "config.h"
#include "wake.h"

static btstack_packet_callback_registration_t hci_event_cb;
static bool stack_ready = false;
static bool scanning = false;
static bool scan_is_active_mode = false;
static uint32_t web_scan_deadline_ms = 0;
static ble_scan_result results[BLE_SCAN_MAX_RESULTS];
static int result_count = 0;

static bool web_scan_active() {
    return to_ms_since_boot(get_absolute_time()) < web_scan_deadline_ms;
}

static void note_result(const uint8_t *addr, uint8_t addr_type, int8_t rssi,
                        const uint8_t *adv, uint8_t adv_len) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());

    ble_scan_result *slot = nullptr;
    for (int i = 0; i < result_count; i++) {
        if (memcmp(results[i].mac, addr, 6) == 0) {
            slot = &results[i];
            break;
        }
    }
    if (!slot) {
        if (result_count < BLE_SCAN_MAX_RESULTS) {
            slot = &results[result_count++];
        } else {
            // table full: evict the longest-unseen entry
            slot = &results[0];
            for (int i = 1; i < BLE_SCAN_MAX_RESULTS; i++) {
                if (results[i].last_seen_ms < slot->last_seen_ms) slot = &results[i];
            }
        }
        memset(slot, 0, sizeof(*slot));
        memcpy(slot->mac, addr, 6);
    }
    slot->addr_type = addr_type;
    slot->rssi = rssi;
    slot->last_seen_ms = now;

    // Pull a device name out of the advertisement if one is included.
    ad_context_t ctx;
    for (ad_iterator_init(&ctx, adv_len, adv); ad_iterator_has_more(&ctx); ad_iterator_next(&ctx)) {
        const uint8_t type = ad_iterator_get_data_type(&ctx);
        if (type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME ||
            type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME) {
            uint8_t len = ad_iterator_get_data_len(&ctx);
            if (len >= sizeof(slot->name)) len = sizeof(slot->name) - 1;
            memcpy(slot->name, ad_iterator_get_data(&ctx), len);
            slot->name[len] = 0;
        }
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void) channel;
    (void) size;
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE: {
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                stack_ready = true;
                printf("[BLE] stack ready\n");
            }
            break;
        }

        case GAP_EVENT_ADVERTISING_REPORT: {
            bd_addr_t addr;
            gap_event_advertising_report_get_address(packet, addr);

            const Config_body &cfg = get_config();
            // Trigger when the host isn't fully up: suspended (S3 -> F15 path)
            // or unmounted (S5 soft-off -> Wake-on-LAN path). wake_trigger()
            // picks the right path based on the observed USB state.
            if (cfg.ble_wake_enabled && (host_suspended || !tud_mounted())) {
                for (int i = 0; i < cfg.device_count; i++) {
                    if (cfg.devices[i].enabled && memcmp(addr, cfg.devices[i].mac, 6) == 0) {
                        printf("[BLE] target %s (%s) seen, waking host\n",
                               bd_addr_to_str(addr), cfg.devices[i].name);
                        wake_trigger();
                        break;
                    }
                }
            }

            if (web_scan_active()) {
                note_result(addr,
                            gap_event_advertising_report_get_address_type(packet),
                            (int8_t) gap_event_advertising_report_get_rssi(packet),
                            gap_event_advertising_report_get_data(packet),
                            gap_event_advertising_report_get_data_length(packet));
            }
            break;
        }

        default:
            break;
    }
}

int ble_init() {
    hci_event_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_event_cb);
    hci_power_control(HCI_POWER_ON);
    return 0;
}

void ble_request_web_scan(uint32_t duration_ms) {
    web_scan_deadline_ms = to_ms_since_boot(get_absolute_time()) + duration_ms;
}

static bool any_device_enabled() {
    const Config_body &cfg = get_config();
    for (int i = 0; i < cfg.device_count; i++) {
        if (cfg.devices[i].enabled) return true;
    }
    return false;
}

void ble_task() {
    if (!stack_ready) return;

    // Scan whenever a wake could be needed: host suspended, or host never
    // enumerated us (PC powered off with USB standby power). The web UI can
    // also force scanning while it polls for nearby devices.
    const bool web = web_scan_active();
    const bool want = web ||
                      (get_config().ble_wake_enabled && any_device_enabled() &&
                       (host_suspended || !tud_mounted()));

    // Web scanning is ACTIVE (requests scan responses, where most devices put
    // their name); the wake path only needs the MAC, so stay passive there.
    if (want != scanning || (want && web != scan_is_active_mode)) {
        if (scanning) gap_stop_scan();
        if (want) {
            // 60 ms interval / 30 ms window (units of 0.625 ms)
            gap_set_scan_parameters(web ? 1 : 0, 0x0060, 0x0030);
            gap_start_scan();
        }
        scanning = want;
        scan_is_active_mode = web;
        printf("[BLE] scan %s%s\n", want ? "on" : "off",
               want ? (web ? " (active)" : " (passive)") : "");
    }
}

bool ble_scanning() {
    return scanning;
}

const ble_scan_result *ble_get_results(int *count) {
    *count = result_count;
    return results;
}
