#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENABLE_SERIAL
#define ENABLE_SERIAL 0
#endif

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
// HID instance 0: boot keyboard that delivers the wake keystroke.
#define CFG_TUD_HID               1
// CDC-NCM network interface carrying the onboard config web UI.
#define CFG_TUD_NCM               1
#define CFG_TUD_ECM_RNDIS         0
// Optional debug serial console.
#define CFG_TUD_CDC               ENABLE_SERIAL
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

// Boot keyboard report is 8 bytes.
#define CFG_TUD_HID_EP_BUFSIZE    8

// CDC FIFO sizes (debug serial only).
#define CFG_TUD_CDC_RX_BUFSIZE   64
#define CFG_TUD_CDC_TX_BUFSIZE   64
#define CFG_TUD_CDC_EP_BUFSIZE   64

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
