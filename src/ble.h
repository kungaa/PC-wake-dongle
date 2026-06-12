#ifndef WAKE_DONGLE_BLE_H
#define WAKE_DONGLE_BLE_H

#include <cstdint>

#define BLE_SCAN_MAX_RESULTS 16

struct ble_scan_result {
    uint8_t mac[6];       // MSB first, as printed
    uint8_t addr_type;
    int8_t rssi;
    char name[21];        // from advertisement local-name field, may be empty
    uint32_t last_seen_ms;
};

int ble_init();
// Manage scan on/off; call from the main loop.
void ble_task();
// Keep the scanner running for the web UI for the given duration.
void ble_request_web_scan(uint32_t duration_ms);
const ble_scan_result *ble_get_results(int *count);
bool ble_scanning();

#endif // WAKE_DONGLE_BLE_H
