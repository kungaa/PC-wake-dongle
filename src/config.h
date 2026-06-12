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

struct __attribute__((packed)) Config_body {
    uint8_t config_version;
    uint8_t ble_wake_enabled; // bool: global wake switch
    uint8_t device_count;     // valid entries in devices[]
    Wake_device devices[WAKE_MAX_DEVICES];
};

struct __attribute__((packed)) Config {
    uint32_t magic;
    uint32_t crc32; // Config_body crc32, calculated on save, verified on load
    uint16_t size;  // sizeof(Config_body)
    Config_body body;
};

void config_load();
bool config_save();
Config_body &get_config();

#endif // WAKE_DONGLE_CONFIG_H
