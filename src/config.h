#ifndef WAKE_DONGLE_CONFIG_H
#define WAKE_DONGLE_CONFIG_H

#include <cstdint>

// 8 devices keeps Config inside one flash page (FLASH_PAGE_SIZE = 256).
#define WAKE_MAX_DEVICES 8
#define WAKE_NAME_LEN 20 // user label, NUL-terminated

struct __attribute__((packed)) Wake_device {
    uint8_t mac[6];           // MSB first (as printed)
    uint8_t enabled;          // bool: this device may wake the PC
    char name[WAKE_NAME_LEN]; // user-editable label
};

// Config-page address selector (Config_body.subnet_index). Values 0..N-1 index
// the vetted preset table; the extra value WAKE_SUBNET_CUSTOM means "use
// Config_body.custom_ip instead" -- power-user escape hatch, still constrained
// to a private RFC-1918 host address (see usb_net.cpp) so it can't point
// somewhere unroutable.
#define WAKE_SUBNET_COUNT  3 // number of fixed presets (0..2)
#define WAKE_SUBNET_CUSTOM WAKE_SUBNET_COUNT // 3 == custom IP
#define WAKE_SUBNET_MAX    WAKE_SUBNET_CUSTOM

struct __attribute__((packed)) Config_body {
    uint8_t config_version;
    uint8_t ble_wake_enabled; // bool: global wake switch
    uint8_t led_off;          // bool: disable the status LED entirely
    uint8_t device_count;     // valid entries in devices[]
    uint8_t subnet_index;     // index into the fixed subnet table, or WAKE_SUBNET_CUSTOM
    // Custom dongle IP (4 octets), used only when subnet_index == WAKE_SUBNET_CUSTOM.
    // Must be a private (RFC-1918) host address; validated in config_load().
    // 0.0.0.0 means "unset" -> falls back to the default preset.
    uint8_t custom_ip[4];
    Wake_device devices[WAKE_MAX_DEVICES];
};

struct __attribute__((packed)) Config {
    uint32_t magic;
    uint32_t crc32; // Config_body crc32, calculated on save, verified on load
    uint16_t size;  // sizeof(Config_body)
    Config_body body;
};

// One entry in the fixed subnet table. Octets MSB-first; the host (DHCP) gets
// the dongle's address + 1.
struct Subnet_info {
    uint8_t dongle_ip[4];
    uint8_t netmask[4];
    const char *label; // shown in the web UI, e.g. "10.7.7.107"
};

void config_load();
bool config_save();
Config_body &get_config();

// The fixed subnet table and the currently-selected entry (clamped to range,
// and falls back to the default preset if subnet_index==WAKE_SUBNET_CUSTOM
// but custom_ip isn't a valid private host address).
const Subnet_info *config_subnet_table();
const Subnet_info &config_active_subnet();

// True if ip[4] is a usable private (RFC-1918) host address for the config
// page: inside 10/8, 172.16/12, or 192.168/16, and not a .0 network / .255
// broadcast within its /29. Shared with the web UI's POST validation.
bool config_ip_is_valid(const uint8_t ip[4]);

#endif // WAKE_DONGLE_CONFIG_H
