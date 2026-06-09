//
// ble_scan.cpp — passive BLE advertisement scanning for PC wake
//
// Registers a BTstack HCI event handler that watches for BLE advertisement
// packets. When the source address matches the MAC configured via web config,
// wake_on_bt_connect() is called — the same function used by the DualSense
// Classic BT connect path.
//

#include "ble_scan.h"
#include "config.h"

#if defined(ENABLE_BLE_WAKE) && defined(ENABLE_WAKE_HID)

#include <cstdio>
#include <cstring>
#include "btstack_event.h"
#include "gap.h"
#include "wake.h"

static btstack_packet_callback_registration_t ble_hci_event_callback_registration;

static void ble_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    (void) channel;
    (void) size;

    if (packet_type != HCI_EVENT_PACKET) return;

    const uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
        case BTSTACK_EVENT_STATE: {
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("[BLE] Stack ready, scan will start on host suspend\n");
            }
            break;
        }

        case GAP_EVENT_ADVERTISING_REPORT: {
            if (!host_suspended) break;
            if (!get_config().ble_wake_enabled) break;
            bd_addr_t addr;
            gap_event_advertising_report_get_address(packet, addr);
            if (memcmp(addr, get_config().ble_wake_mac, 6) == 0) {
                printf("[BLE] Target BLE device detected, triggering wake\n");
                wake_on_bt_connect();
            }
            break;
        }

        default:
            break;
    }
}

void ble_scan_init(void) {
    ble_hci_event_callback_registration.callback = &ble_packet_handler;
    hci_add_event_handler(&ble_hci_event_callback_registration);
    printf("[BLE] BLE scan handler registered\n");
}

#endif // ENABLE_BLE_WAKE && ENABLE_WAKE_HID

