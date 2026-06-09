#include "pin_entry.h"
#include "config.h"
#include "tusb.h"
#include "class/hid/hid.h"
#include "pico/time.h"

#define PIN_KBD_INSTANCE 1
#define KEY_DELAY_MS     0
#define KEY_DURATION_MS  30

static const uint8_t HID_DIGIT_KEYS[10] = {
    HID_KEY_0, HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4,
    HID_KEY_5, HID_KEY_6, HID_KEY_7, HID_KEY_8, HID_KEY_9
};

typedef enum {
    PIN_IDLE,
    PIN_TYPING,
    PIN_DONE
} pin_state_t;

static pin_state_t state       = PIN_IDLE;
static uint8_t     digit_index = 0;
static uint32_t    next_action = 0;
static bool        key_down    = false;

void pin_entry_reset() {
    if (key_down && tud_hid_n_ready(PIN_KBD_INSTANCE))
        tud_hid_n_keyboard_report(PIN_KBD_INSTANCE, 0, 0, NULL);
    state       = PIN_IDLE;
    digit_index = 0;
    next_action = 0;
    key_down    = false;
}

void pin_entry_tick(const uint8_t *data, uint16_t len) {
    if (len < 10) return;
    Config_body &cfg = get_config();
    if (!cfg.pin_enabled) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint8_t digit = cfg.pin_digits[digit_index];

    bool dpad_left = (data[7] & 0x0F) == 6;
    bool circle    = (data[7] & 0x40) != 0;
    bool l1        = (data[8] & 0x01) != 0;
    bool r1        = (data[8] & 0x02) != 0;
    bool combo     = dpad_left && circle && l1 && r1;

    switch (state) {
        case PIN_IDLE:
            if (combo) {
                state       = PIN_TYPING;
                digit_index = 0;
                next_action = now;
                key_down    = false;
            }
            break;

        case PIN_TYPING:
            if (digit_index >= 4) {
                if (key_down && tud_hid_n_ready(PIN_KBD_INSTANCE)) {
                    tud_hid_n_keyboard_report(PIN_KBD_INSTANCE, 0, 0, NULL);
                    key_down = false;
                }
                state = PIN_DONE;
                break;
            }
            if (now < next_action) break;

            if (!key_down) {
                uint8_t digit = cfg.pin_digits[digit_index];
                if (digit > 9) digit = 0;
                if (tud_hid_n_ready(PIN_KBD_INSTANCE)) {
                    uint8_t keys[6] = { HID_DIGIT_KEYS[digit] };
                    tud_hid_n_keyboard_report(PIN_KBD_INSTANCE, 0, 0, keys);
                    key_down    = true;
                    next_action = now + KEY_DURATION_MS;
                }
            } else {
                if (tud_hid_n_ready(PIN_KBD_INSTANCE)) {
                    tud_hid_n_keyboard_report(PIN_KBD_INSTANCE, 0, 0, NULL);
                    key_down    = false;
                    digit_index++;
                    next_action = now + KEY_DELAY_MS;
                }
            }
            break;

        case PIN_DONE:
            if (!combo) state = PIN_IDLE;
            break;
    }
}