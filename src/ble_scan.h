//
// ble_scan.h — passive BLE advertisement scanning for Xbox Series X wake
//

#ifndef BLE_SCAN_H
#define BLE_SCAN_H

#ifdef ENABLE_BLE_WAKE

#ifdef __cplusplus
extern "C" {
#endif

// Call once after bt_init() / hci_power_control(HCI_POWER_ON).
// Registers a BLE advertisement callback; when the Xbox controller MAC is
// seen, calls wake_on_bt_connect() to trigger USB remote wakeup.
void ble_scan_init(void);

#ifdef __cplusplus
}
#endif

#endif // ENABLE_BLE_WAKE

#endif // BLE_SCAN_H
