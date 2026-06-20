#include "config.h"

#include <cstdio>
#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"

// New magic vs. the DS5-dongle ancestor so stale configs are discarded.
constexpr uint32_t CONFIG_MAGIC = 0x57414B45; // "WAKE"
// v2: multi-device list; v3: led_off; v4: subnet_index; v5: custom_ip
constexpr uint8_t CONFIG_VERSION = 5;
constexpr uint32_t CONFIG_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

static Config config{};

// Vetted, selectable subnet presets. All /29; the host (DHCP) gets dongle_ip + 1.
// Index 0 is the historical default (10.7.7.107) so existing users are
// unaffected by the upgrade. A fixed list means a user can never pick a preset
// that locks them out of the config page; WAKE_SUBNET_CUSTOM (below) is the
// escape hatch for a user-entered IP, gated by config_ip_is_valid().
static const Subnet_info subnet_table[WAKE_SUBNET_COUNT] = {
    {{10, 7, 7, 107},      {255, 255, 255, 248}, "10.7.7.107"},
    {{172, 31, 7, 107},    {255, 255, 255, 248}, "172.31.7.107"},
    {{192, 168, 137, 107}, {255, 255, 255, 248}, "192.168.137.107"},
};

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

bool config_ip_is_valid(const uint8_t ip[4]) {
    const uint8_t a = ip[0], b = ip[1], d = ip[3];
    const bool rfc1918 =
        (a == 10) ||
        (a == 172 && b >= 16 && b <= 31) ||
        (a == 192 && b == 168);
    if (!rfc1918) return false;
    // Keep the dongle host octet in [1,252] so its /29 (network = d & ~7) has
    // room for the +1 host lease without hitting .255 broadcast.
    if (d < 1 || d > 252) return false;
    const uint8_t net = d & 0xF8; // /29 network base
    if (d == net) return false;       // network address
    if (d == (net + 7)) return false; // /29 broadcast
    return true;
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
    if (config.body.subnet_index > WAKE_SUBNET_MAX) {
        config.body.subnet_index = 0;
    }
    // If "custom IP" is selected, the stored address must be a valid private
    // host address; otherwise fall back to the default preset so the page
    // stays reachable (the safety net behind the custom-IP escape hatch).
    if (config.body.subnet_index == WAKE_SUBNET_CUSTOM &&
        !config_ip_is_valid(config.body.custom_ip)) {
        config.body.subnet_index = 0;
        printf("[Config] custom_ip invalid; using default preset\n");
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

const Subnet_info *config_subnet_table() {
    return subnet_table;
}

const Subnet_info &config_active_subnet() {
    uint8_t idx = config.body.subnet_index;
    if (idx >= WAKE_SUBNET_COUNT) idx = 0;
    return subnet_table[idx];
}
