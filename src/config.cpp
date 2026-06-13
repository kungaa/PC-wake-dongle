#include "config.h"

#include <cstdio>
#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"

// New magic vs. the DS5-dongle ancestor so stale configs are discarded.
constexpr uint32_t CONFIG_MAGIC = 0x57414B45; // "WAKE"
constexpr uint8_t CONFIG_VERSION = 3;         // v2: multi-device list; v3: led_off
constexpr uint32_t CONFIG_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

static Config config{};

static_assert(sizeof(Config) <= FLASH_PAGE_SIZE);
static_assert(CONFIG_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

static uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

static uint32_t calc_config_crc(const Config &con) {
    return crc32(reinterpret_cast<const uint8_t *>(&con.body), sizeof(Config_body));
}

static const Config *flash_config() {
    return reinterpret_cast<const Config *>(XIP_BASE + CONFIG_FLASH_OFFSET);
}

static void config_defaults() {
    config = {};
    config.magic = CONFIG_MAGIC;
    config.size = sizeof(Config_body);
    config.body.config_version = CONFIG_VERSION;
    // Wake on by default: a freshly added device should just work.
    config.body.ble_wake_enabled = 1;
}

void config_load() {
    memcpy(&config, flash_config(), sizeof(Config));

    if (config.magic != CONFIG_MAGIC ||
        config.size != sizeof(Config_body) ||
        config.body.config_version != CONFIG_VERSION ||
        config.crc32 != calc_config_crc(config)) {
        printf("[Config] no valid config in flash, using defaults\n");
        config_defaults();
        return;
    }
    if (config.body.ble_wake_enabled > 1) {
        config.body.ble_wake_enabled = 0;
    }
    if (config.body.led_off > 1) {
        config.body.led_off = 0;
    }
    if (config.body.device_count > WAKE_MAX_DEVICES) {
        config.body.device_count = WAKE_MAX_DEVICES;
    }
    for (int i = 0; i < config.body.device_count; i++) {
        Wake_device &d = config.body.devices[i];
        if (d.enabled > 1) d.enabled = 0;
        d.name[WAKE_NAME_LEN - 1] = 0;
    }
    printf("[Config] loaded: wake %s, %d device(s)\n",
           config.body.ble_wake_enabled ? "enabled" : "disabled",
           config.body.device_count);
}

bool config_save() {
    config.crc32 = calc_config_crc(config);
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &config, sizeof(Config));

    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_FLASH_OFFSET, page, sizeof(page));
    restore_interrupts(interrupts);

    Config verify{};
    memcpy(&verify, flash_config(), sizeof(verify));
    if (calc_config_crc(verify) == config.crc32) {
        printf("[Config] flash write verified\n");
        return true;
    }
    printf("[Config] flash write verify FAILED\n");
    return false;
}

Config_body &get_config() {
    return config.body;
}
