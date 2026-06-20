//
// wake.cpp -- USB remote-wakeup + fake F15 keystroke state machine.
//
// Inherited from the DS5 dongle's ENABLE_WAKE_HID feature; the FSM and its
// OS quirk handling (Windows wake-and-resleep, Linux remote-wakeup flag,
// hubs masking suspend) are kept intact. Triggering is now done exclusively
// by the BLE scanner via wake_trigger().
//

#include "wake.h"

#include <cstdio>
#include <cstring>
#include "tusb.h"
#include "device/dcd.h"
#include "pico/sync.h"
#include "pico/time.h"

#include "config.h"

#define WAKE_KBD_INSTANCE     0
#define WAKE_KEYCODE_F15      0x68
// Post-resume timings tuned for "wake-and-resleep" Windows behavior: the host
// resumes USB, but if no HID input is consumed during the brief wake window
// the system can re-suspend within ~1 s. Bigger settles + a second F15 give
// Windows multiple polling cycles to pick the keystroke up.
#define WAKE_SETTLE_US        150000   // 150 ms -- let host finish USB re-init
#define WAKE_KEY_HOLD_US       80000   // 80 ms keydown -> keyup gap
#define WAKE_KEY_UP_SETTLE_US 200000   // 200 ms between attempts (or before DONE)
#define WAKE_REQUEST_TIMEOUT_US 5000000
#define WAKE_KEY_ATTEMPTS     2

#ifdef WAKE_DEBUG
#  define WAKE_DBG(fmt, ...) printf("[wake] " fmt "\n", ##__VA_ARGS__)
static const char *wake_state_name(int s) {
    switch (s) {
    case 0: return "IDLE";
    case 1: return "PENDING_PRESS";
    case 2: return "REQUESTED";
    case 3: return "KEY_DOWN";
    case 4: return "KEY_UP_SENT";
    case 5: return "DONE";
    default: return "?";
    }
}
#else
#  define WAKE_DBG(fmt, ...) ((void)0)
#endif

typedef enum {
    WAKE_IDLE,
    WAKE_PENDING_PRESS,
    WAKE_REQUESTED,
    WAKE_KEY_DOWN,
    WAKE_KEY_UP_SENT,
    WAKE_DONE,
} wake_state_t;

static critical_section_t wake_cs;
volatile bool host_suspended = false;
static volatile bool host_resumed_event = false;

// Guard so we send at most one WOL packet per suspend spell. On this class of
// hardware the host enters S5 (soft-off) and S3 (sleep) the SAME way over USB
// -- tud_suspend_cb fires, then the device may go unmounted, all with no signal
// that distinguishes the two (verified on real hardware: tud_umount_cb never
// fires and the "unmounted" state always coincides with suspended). So we
// cannot tell S3 from S5. Instead, on a wake trigger while suspended we fire
// BOTH paths: the F15 keystroke (wakes S3 / USB-HID-wake boards) AND a
// Wake-on-LAN packet (wakes S5 via the NIC). A stray WOL during S3 is harmless.
// This flag rate-limits the WOL to once per suspend spell (the BLE scanner can
// trigger many times a second while the target advertises).
static volatile bool wol_fired_this_spell = false;
// Hard time floor between WOL sends, surviving the spell-flag churn during an
// S3->S5 transition. ms-since-boot of the last send; 0 = never.
static volatile uint32_t last_wol_ms = 0;
#define WAKE_WOL_MIN_INTERVAL_MS 10000
static wake_state_t state = WAKE_IDLE;
static uint64_t state_entered_us = 0;
static uint8_t key_attempts = 0;

static void enter_state(wake_state_t s) {
    state = s;
    state_entered_us = time_us_64();
}

static void request_host_wake(const char *reason) {
    bool ok = tud_remote_wakeup();

    // Linux quirk: Sometimes Linux fails to set the REMOTE_WAKEUP feature
    // flag before the second suspend, causing TinyUSB to refuse to wake.
    // If we are suspended but ok is false, we force the wake signal.
    if (!ok && host_suspended) {
        WAKE_DBG("%s: tud_remote_wakeup()=0 but suspended. Forcing DCD wake.", reason);
        dcd_remote_wakeup(0);
        ok = true;
    }

    if (ok) {
        critical_section_enter_blocking(&wake_cs);
        state = WAKE_REQUESTED;
        state_entered_us = time_us_64();
        critical_section_exit(&wake_cs);
        WAKE_DBG("%s -> REQUESTED", reason);
    }
#ifdef WAKE_DEBUG
    else {
        static uint64_t last_log = 0;
        const uint64_t now = time_us_64();
        if (now - last_log > 5000000) {
            WAKE_DBG("%s, tud_remote_wakeup()=0 (USB bus not in suspend) -- 5s heartbeat", reason);
            last_log = now;
        }
    }
#endif
}

void wake_init(void) {
    critical_section_init(&wake_cs);
}

extern "C" void tud_suspend_cb(bool remote_wakeup_en) {
    WAKE_DBG("tud_suspend_cb remote_wakeup_en=%d prev_state=%s",
             (int)remote_wakeup_en, wake_state_name(state));
    host_suspended = true;
    host_resumed_event = false;
    wol_fired_this_spell = false; // new suspend spell -> allow one WOL again

    // Unconditionally re-arm on suspend. If a previous wake attempt hung
    // (e.g. Linux ignored a keystroke and left the endpoint busy forever),
    // we must abort and reset so the NEXT wake attempt can trigger.
    state = WAKE_PENDING_PRESS;
    state_entered_us = time_us_64();
    key_attempts = 0;
    WAKE_DBG("-> PENDING_PRESS");
}

extern "C" void tud_resume_cb(void) {
    WAKE_DBG("tud_resume_cb state=%s", wake_state_name(state));
    host_suspended = false;
    host_resumed_event = true;
}

extern "C" void tud_mount_cb(void) {
    WAKE_DBG("tud_mount_cb state=%s", wake_state_name(state));
    host_suspended = false;
    host_resumed_event = true;
}

// Set by wake_trigger() (BLE callback context) to ask the main loop to send a
// WOL packet; consumed by wake_wol_pending() from wifi_task. Deferring the send
// keeps lwIP/cyw43 work out of the BTstack callback (which itself runs inside
// cyw43_arch_poll) and avoids re-entrancy.
static volatile bool wol_send_requested = false;

void wake_trigger(void) {
    critical_section_enter_blocking(&wake_cs);
    const bool should_wake = host_suspended &&
        (state == WAKE_IDLE || state == WAKE_DONE || state == WAKE_PENDING_PRESS);
    const bool suspended = host_suspended;
    critical_section_exit(&wake_cs);

    if (should_wake) {
        request_host_wake("BLE target while suspended");
    }
    // S3 vs S5 is indistinguishable over USB on this hardware, so whenever the
    // host is suspended we ALSO request Wake-on-LAN: F15 above handles S3 (and
    // USB-HID-wake boards), WOL handles S5. Harmless if the host was only in S3.
    // The actual send is deferred to the main loop (wake_wol_pending) to stay
    // out of the BLE callback.
    //
    // Two-layer rate limit: at most once per suspend spell AND a hard time floor
    // between sends. During the S3->S5 transition the host re-suspends in a
    // tight loop (each forced DCD wake), which re-arms wol_fired_this_spell many
    // times a second; the time floor stops that from becoming a WOL/ioctl storm
    // (one such storm produced a [CYW43] do_ioctl timeout in testing).
    if (suspended && !wol_fired_this_spell) {
        const uint32_t ms = to_ms_since_boot(get_absolute_time());
        if (last_wol_ms == 0 || (ms - last_wol_ms) >= WAKE_WOL_MIN_INTERVAL_MS) {
            wol_send_requested = true;
            last_wol_ms = ms;
        }
        wol_fired_this_spell = true;
    }
}

bool wake_wol_pending(void) {
    if (!wol_send_requested) return false;
    wol_send_requested = false;
    const Config_body &c = get_config();
    if (!c.wol_enabled) return false;
    bool mac_set = false;
    for (int i = 0; i < 6; i++) if (c.wol_target_mac[i]) { mac_set = true; break; }
    return mac_set;
}

void wake_get_wol_mac(uint8_t out[6]) {
    memcpy(out, get_config().wol_target_mac, 6);
}

void wake_task(void) {
    const uint64_t now = time_us_64();

#ifdef WAKE_DEBUG
    // 1 Hz heartbeat of the USB power-state signals we use to pick the wake
    // path. This is how we tell, on real hardware, whether a board's S5 looks
    // like "unmounted" (WOL path) or "still mounted" (looks awake -> no wake).
    {
        static uint64_t last_hb = 0;
        if (now - last_hb > 1000000) {
            last_hb = now;
            WAKE_DBG("hb: mounted=%d suspended=%d wol_fired=%d",
                     (int) tud_mounted(), (int) host_suspended,
                     (int) wol_fired_this_spell);
        }
    }
#endif

    critical_section_enter_blocking(&wake_cs);
    const wake_state_t s = state;
    const uint64_t entered = state_entered_us;
    critical_section_exit(&wake_cs);

    switch (s) {
        case WAKE_IDLE:
        case WAKE_PENDING_PRESS:
        case WAKE_DONE:
            return;

        case WAKE_REQUESTED: {
            if (host_resumed_event || !host_suspended) {
                host_resumed_event = false;
                if (now - entered < WAKE_SETTLE_US) return;
                if (!tud_hid_n_ready(WAKE_KBD_INSTANCE)) {
#ifdef WAKE_DEBUG
                    static uint64_t last_log = 0;
                    if (now - last_log > 1000000) {
                        WAKE_DBG("REQUESTED waiting: hid_n_ready=0 (heartbeat 1Hz)");
                        last_log = now;
                    }
#endif
                    return;
                }
                uint8_t rpt[8] = { 0, 0, WAKE_KEYCODE_F15, 0, 0, 0, 0, 0 };
                const bool sent = tud_hid_n_report(WAKE_KBD_INSTANCE, 0, rpt, sizeof(rpt));
                WAKE_DBG("REQUESTED: sent keydown 0x%02X -> %d", WAKE_KEYCODE_F15, (int)sent);
                if (sent) {
                    critical_section_enter_blocking(&wake_cs);
                    enter_state(WAKE_KEY_DOWN);
                    critical_section_exit(&wake_cs);
                }
            } else if (now - entered > WAKE_REQUEST_TIMEOUT_US) {
                WAKE_DBG("REQUESTED timeout 5s -> DONE (no resume signaling; may have already woken)");
                critical_section_enter_blocking(&wake_cs);
                enter_state(WAKE_DONE);
                critical_section_exit(&wake_cs);
            }
            return;
        }

        case WAKE_KEY_DOWN: {
            if (now - entered < WAKE_KEY_HOLD_US) return;
            if (!tud_hid_n_ready(WAKE_KBD_INSTANCE)) {
#ifdef WAKE_DEBUG
                static uint64_t last_log = 0;
                if (now - last_log > 1000000) {
                    WAKE_DBG("KEY_DOWN waiting: hid_n_ready=0 (heartbeat 1Hz)");
                    last_log = now;
                }
#endif
                return;
            }
            uint8_t up[8] = { 0 };
            const bool sent = tud_hid_n_report(WAKE_KBD_INSTANCE, 0, up, sizeof(up));
            WAKE_DBG("KEY_DOWN: sent keyup -> %d", (int)sent);
            if (sent) {
                critical_section_enter_blocking(&wake_cs);
                enter_state(WAKE_KEY_UP_SENT);
                critical_section_exit(&wake_cs);
            }
            return;
        }

        case WAKE_KEY_UP_SENT: {
            if (now - entered < WAKE_KEY_UP_SETTLE_US) return;
            key_attempts++;
            if (key_attempts < WAKE_KEY_ATTEMPTS) {
                // Retry: do NOT re-enter WAKE_REQUESTED (which gates on a
                // fresh tud_resume_cb event). We already established the
                // host woke once; just send another keydown directly. If the
                // host has dipped back into suspend, tud_hid_n_ready will be
                // false and we'll heartbeat from KEY_DOWN until it returns.
                if (!tud_hid_n_ready(WAKE_KBD_INSTANCE)) {
#ifdef WAKE_DEBUG
                    static uint64_t last_log = 0;
                    if (now - last_log > 1000000) {
                        WAKE_DBG("KEY_UP_SENT retry waiting: hid_n_ready=0 (heartbeat 1Hz)");
                        last_log = now;
                    }
#endif
                    return;
                }
                uint8_t rpt[8] = { 0, 0, WAKE_KEYCODE_F15, 0, 0, 0, 0, 0 };
                const bool sent = tud_hid_n_report(WAKE_KBD_INSTANCE, 0, rpt, sizeof(rpt));
                WAKE_DBG("KEY_UP_SENT: retrying F15 (attempt %d/%d) -> %d",
                         (int)key_attempts + 1, (int)WAKE_KEY_ATTEMPTS, (int)sent);
                if (sent) {
                    critical_section_enter_blocking(&wake_cs);
                    enter_state(WAKE_KEY_DOWN);
                    critical_section_exit(&wake_cs);
                }
            } else {
                WAKE_DBG("KEY_UP_SENT settle done -> DONE");
                critical_section_enter_blocking(&wake_cs);
                enter_state(WAKE_DONE);
                key_attempts = 0;
                critical_section_exit(&wake_cs);
            }
            return;
        }
    }
}
