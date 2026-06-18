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

// Fixed list of selectable subnets (index stored in Config_body.subnet_index).
// A fixed list, not a free-form IP, so a user can never lock themselves out.
#define WAKE_SUBNET_COUNT 4

struct __attribute__((packed)) Config_body {
    uint8_t config_version;
    uint8_t ble_wake_enabled; // bool: global wake switch
    uint8_t led_off;          // bool: disable the status LED entirely
    uint8_t device_count;     // valid entries in devices[]
    uint8_t subnet_index;     // index into the fixed subnet table (0..WAKE_SUBNET_COUNT-1)
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

// The fixed subnet table and the currently-selected entry (clamped to range).
const Subnet_info *config_subnet_table();
const Subnet_info &config_active_subnet();

#endif // WAKE_DONGLE_CONFIG_H
