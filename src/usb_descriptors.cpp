//
// usb_descriptors.cpp -- composite device: HID boot keyboard (wake) +
// CDC-NCM network interface (config web UI) + optional CDC debug serial.
//

#include <cstdio>
#include <cstring>

#include "bsp/board_api.h"
#include "tusb.h"

#ifndef ENABLE_SERIAL
#define ENABLE_SERIAL 0
#endif

//--------------------------------------------------------------------+
// Interface / endpoint / string layout
//--------------------------------------------------------------------+

enum {
    ITF_NUM_HID_KBD = 0,
    ITF_NUM_NET,        // NCM control (IAD spans control + data)
    ITF_NUM_NET_DATA,
#if ENABLE_SERIAL
    ITF_NUM_CDC,
    ITF_NUM_CDC_DATA,
#endif
    ITF_NUM_TOTAL
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_NET,
    STRID_MAC,
#if ENABLE_SERIAL
    STRID_CDC,
#endif
};

#define EPNUM_HID_KBD_IN  0x81
#define EPNUM_NET_NOTIF   0x82
#define EPNUM_NET_OUT     0x02
#define EPNUM_NET_IN      0x83
#if ENABLE_SERIAL
#define EPNUM_CDC_NOTIF   0x84
#define EPNUM_CDC_OUT     0x03
#define EPNUM_CDC_IN      0x85
#endif

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,

    // IADs (used by NCM/CDC) require the misc/common/IAD device class triple.
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    // TinyUSB's openly usable test VID/PID space -- this is a hobby device.
    .idVendor = 0xCafe,
    .idProduct = 0x4020,
    .bcdDevice = 0x0100,

    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return reinterpret_cast<uint8_t const *>(&desc_device);
}

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

// 45-byte boot-keyboard report descriptor (modifier byte + reserved + 6
// keycodes, no Report ID -- boot protocol forbids one). Kept byte-identical
// to the field-tested DS5-dongle wake build.
uint8_t const desc_hid_report_kbd[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       //   Usage Minimum (Left Control)
    0x29, 0xE7,       //   Usage Maximum (Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data,Var,Abs) -- modifier byte
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Const) -- reserved byte
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0x65,       //   Usage Maximum (101)
    0x81, 0x00,       //   Input (Data,Array) -- 6 keycodes
    0xC0              // End Collection
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
    (void) itf;
    return desc_hid_report_kbd;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_NCM_DESC_LEN \
                          + (ENABLE_SERIAL ? TUD_CDC_DESC_LEN : 0))

uint8_t const desc_configuration[] = {
    // Self-powered + remote-wakeup, 500 mA: matches the field-tested wake
    // build (some hosts only arm wake for the exact attribute combination
    // that was validated).
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    // HID boot keyboard, 8-byte report, 10 ms polling
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_KBD, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_hid_report_kbd), EPNUM_HID_KBD_IN, CFG_TUD_HID_EP_BUFSIZE, 10),

    // CDC-NCM: notif EP + bulk data EPs, MTU-sized segments
    TUD_CDC_NCM_DESCRIPTOR(ITF_NUM_NET, STRID_NET, STRID_MAC, EPNUM_NET_NOTIF, 64,
                           EPNUM_NET_OUT, EPNUM_NET_IN, 64, CFG_TUD_NET_MTU, 50, 0),

#if ENABLE_SERIAL
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
#endif
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: language: English (0x0409)
    "PC Wake Dongle",           // 1: Manufacturer
    "BLE PC Wake Dongle",       // 2: Product
    NULL,                       // 3: Serial (board unique id)
    "Config Network",           // 4: NCM interface
    NULL,                       // 5: MAC address (generated)
#if ENABLE_SERIAL
    "Debug Serial",             // 6: CDC interface
#endif
};

static uint16_t _desc_str[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    size_t chr_count;

    switch (index) {
        case STRID_LANGID:
            memcpy(&_desc_str[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;

        case STRID_SERIAL:
            chr_count = board_usb_get_serial(_desc_str + 1, 32);
            break;

        case STRID_MAC: {
            // MAC the host NIC should use, 12 hex chars (CDC-ECM convention).
            extern uint8_t tud_network_mac_address[6];
            chr_count = 0;
            for (int i = 0; i < 6; i++) {
                _desc_str[1 + chr_count++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 4) & 0xf];
                _desc_str[1 + chr_count++] = "0123456789ABCDEF"[tud_network_mac_address[i] & 0xf];
            }
            break;
        }

        default: {
            if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;
            const char *str = string_desc_arr[index];
            if (!str) return NULL;

            chr_count = strlen(str);
            size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
            if (chr_count > max_count) chr_count = max_count;

            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = str[i];
            }
            break;
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

//--------------------------------------------------------------------+
// HID callbacks (keyboard is output-only; reports are sent by wake.cpp)
//--------------------------------------------------------------------+

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void) itf;
    (void) report_id;
    (void) report_type;
    if (reqlen >= 8) {
        memset(buffer, 0, 8); // idle key state
        return 8;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    // Host LED state (caps lock etc.) -- ignore.
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}
