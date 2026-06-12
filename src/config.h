#ifndef WAKE_DONGLE_CONFIG_H
#define WAKE_DONGLE_CONFIG_H

#include <cstdint>

struct __attribute__((packed)) Config_body {
    uint8_t config_version;
    uint8_t ble_wake_enabled; // bool: 0 disabled, 1 enabled
    uint8_t ble_wake_mac[6];  // target BLE device MAC, MSB first (as printed)
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
