#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// LE-only build: the dongle is a passive BLE observer (no Classic, no
// peripheral role, no GATT). Sizes follow the pico-examples LE configs.

#ifndef ENABLE_BLE
#define ENABLE_BLE
#endif
#define ENABLE_LE_CENTRAL
// Not used, but btstack's hci.c does not compile central-only.
#define ENABLE_LE_PERIPHERAL

#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

// HCI buffers: we only consume advertising reports -- no ACL traffic.
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (255 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
#define MAX_NR_HCI_ACL_PACKETS 4
#define MAX_NR_HCI_CONNECTIONS 1

#define MAX_NR_L2CAP_CHANNELS 2
#define MAX_NR_L2CAP_SERVICES 2

#define MAX_NR_GATT_CLIENTS 1
#define MAX_ATT_DB_SIZE 256
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 8
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4

#define NVM_NUM_LINK_KEYS 4
#define NVM_NUM_DEVICE_DB_ENTRIES 4

#define HAVE_EMBEDDED_TIME_MS

#endif
