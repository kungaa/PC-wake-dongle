#ifndef WAKE_DONGLE_WAKE_H
#define WAKE_DONGLE_WAKE_H

#include <cstdint>

void wake_init(void);
// Request a host wake. Called when a configured BLE device is seen advertising.
// When the host is suspended this taps F15 over USB (wakes S3 / USB-HID-wake
// boards) and ALSO requests a Wake-on-LAN packet (wakes S5) -- the two states
// are indistinguishable over USB on this hardware, so we fire both. No-op when
// the host is awake.
void wake_trigger(void);
void wake_task(void);

// Main-loop hand-off for the WOL send (kept out of the BLE callback). Returns
// true once per suspend spell when a WOL packet should be sent; the caller then
// reads the target MAC via wake_get_wol_mac() and calls wifi_send_wol().
bool wake_wol_pending(void);
void wake_get_wol_mac(uint8_t out[6]);

extern volatile bool host_suspended;

#endif // WAKE_DONGLE_WAKE_H
