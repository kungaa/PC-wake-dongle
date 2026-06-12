//
// PC Wake Dongle: wakes the host PC from sleep (or soft-off, on boards that
// keep USB powered) when a configured BLE device starts advertising --
// useful for gamepads that cannot wake a PC themselves.
//
//   - USB HID boot keyboard with remote wakeup: taps F15 to wake the host
//   - passive BLE scan (CYW43) matches the configured target MAC
//   - USB NCM network interface serves the config web UI (192.168.7.1)
//

#include <cstdio>

#include "bsp/board_api.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "tusb.h"
#if ENABLE_SERIAL
#include "pico/stdio_usb.h"
#endif

#include "ble.h"
#include "config.h"
#include "usb_net.h"
#include "wake.h"

// LED: solid while the host is up, 1 Hz blink while scanning, off otherwise.
// The LED sits behind the CYW43 SPI bridge, so only write it on change.
static void led_task() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    static uint32_t last_toggle = 0;
    static bool blink_on = false;
    static int applied = -1;

    bool want;
    if (ble_scanning()) {
        if (now - last_toggle >= 500) {
            last_toggle = now;
            blink_on = !blink_on;
        }
        want = blink_on;
    } else {
        want = tud_mounted() && !host_suspended;
    }

    if ((int) want != applied) {
        applied = want;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, want);
    }
}

int main() {
    board_init();
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
    board_init_after_tusb();
#if ENABLE_SERIAL
    stdio_usb_init();
#endif

    config_load();
    wake_init();
    usb_net_init();

    if (cyw43_arch_init()) {
        printf("Failed to initialise CYW43\n");
        return 1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    ble_init();

#if !ENABLE_SERIAL
    if (watchdog_caused_reboot()) {
        printf("Rebooted by watchdog!\n");
    }
    watchdog_enable(1000, true);
#endif

    while (true) {
#if !ENABLE_SERIAL
        watchdog_update();
#endif
        cyw43_arch_poll();
        tud_task();
        usb_net_task();
        ble_task();
        wake_task();
        led_task();
    }
}
