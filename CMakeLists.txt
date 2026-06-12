# PC Wake Dongle: BLE-scan triggered USB remote wakeup (fake boot keyboard)
# with an onboard config web UI served over a USB NCM network interface.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if (WIN32)
    set(USERHOME $ENV{USERPROFILE})
else ()
    set(USERHOME $ENV{HOME})
endif ()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif ()
# ====================================================================================

option(PICO_W_BUILD "Build for Raspberry Pi Pico W (RP2040)" OFF)
option(WAVESHARE_RP2350B_PLUS_W_BUILD "Build for Waveshare RP2350B-Plus-W (USB-C, 16MB flash, RM2 wireless)" OFF)

if (PICO_W_BUILD AND WAVESHARE_RP2350B_PLUS_W_BUILD)
    message(FATAL_ERROR "PICO_W_BUILD and WAVESHARE_RP2350B_PLUS_W_BUILD are mutually exclusive")
endif ()

if (PICO_W_BUILD)
    set(PICO_BOARD pico_w CACHE STRING "Board type")
    set(PICO_RP2350 OFF CACHE BOOL "Not using RP2350")
elseif (WAVESHARE_RP2350B_PLUS_W_BUILD)
    set(PICO_BOARD waveshare_rp2350b_plus_w CACHE STRING "Board type")
    # Waveshare board header is not in upstream pico-sdk; ship it here and
    # tell the SDK to search this directory when resolving PICO_BOARD.
    list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/boards/headers")
else ()
    set(PICO_BOARD pico2_w CACHE STRING "Board type")
endif ()

option(ENABLE_SERIAL "Enable USB CDC serial printf output" OFF)
if (ENABLE_SERIAL)
    set(ENABLE_SERIAL_VALUE 1)
else ()
    set(ENABLE_SERIAL_VALUE 0)
endif ()
option(ENABLE_VERBOSE "Enable Log Verbose" OFF)
if (ENABLE_VERBOSE)
    set(ENABLE_VERBOSE_VALUE 1)
else ()
    set(ENABLE_VERBOSE_VALUE 0)
endif ()
option(WAKE_DEBUG "Print wake-FSM transitions to UART (developer use)" OFF)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pc-wake-dongle C CXX ASM)

pico_sdk_init()

add_executable(pc-wake-dongle
        src/main.cpp
        src/wake.cpp
        src/ble.cpp
        src/config.cpp
        src/usb_descriptors.cpp
        src/usb_net.cpp
        # TinyUSB's tiny DHCP server, used to hand the host an address on the
        # USB NCM link.
        ${PICO_TINYUSB_PATH}/lib/networking/dhserver.c
)

target_include_directories(pc-wake-dongle PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${PICO_TINYUSB_PATH}/lib/networking
)

target_compile_definitions(pc-wake-dongle PRIVATE
        # lwIP runs over the USB NCM interface only; the CYW43 is BT-only.
        CYW43_LWIP=0
        ENABLE_SERIAL=${ENABLE_SERIAL_VALUE}
        ENABLE_VERBOSE=${ENABLE_VERBOSE_VALUE}
        PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
        PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0
        PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
)

if (WAKE_DEBUG)
    target_compile_definitions(pc-wake-dongle PRIVATE WAKE_DEBUG)
endif ()

set(VERSION "dev" CACHE STRING "Program version string")

pico_set_program_name(pc-wake-dongle "pc-wake-dongle")
pico_set_program_version(pc-wake-dongle "${VERSION}")

pico_enable_stdio_uart(pc-wake-dongle 1)
pico_enable_stdio_usb(pc-wake-dongle ${ENABLE_SERIAL_VALUE})

target_link_libraries(pc-wake-dongle
        pico_stdlib
        pico_unique_id
        hardware_watchdog
        hardware_flash
        pico_cyw43_arch_poll
        pico_btstack_ble
        pico_btstack_cyw43
        pico_lwip_nosys
        pico_lwip_http
        pico_lwip_mdns
        tinyusb_device
        tinyusb_board
)

pico_add_extra_outputs(pc-wake-dongle)
