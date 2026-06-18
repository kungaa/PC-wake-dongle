# PC Wake Dongle

> Turn a Raspberry Pi Pico W / Pico 2 W into a USB dongle that wakes your PC
> from sleep (and from soft-off on compatible motherboards) when a Bluetooth LE
> device — e.g. a gamepad — powers on.

Most Bluetooth gamepads cannot wake a sleeping PC: you turn the pad on, it
tries to reconnect, but the PC's Bluetooth stack is asleep and nothing
happens. This dongle fixes that — out of the box, with no per-device OS
tweaking (no fiddling with Device Manager power settings on Windows, no
editing udev rules on Linux); it wakes the PC as an ordinary USB keyboard:

1. It enumerates as a **USB boot keyboard** with remote wakeup.
2. While the host sleeps, it runs a **passive BLE scan** on the Pico's CYW43
   radio.
3. When the configured device's MAC address shows up in an advertisement, the
   dongle signals USB remote wakeup and taps **F15** (a harmless key) to keep
   Windows from immediately re-suspending.

Configuration happens in your browser: the dongle also enumerates as a **USB
network adapter** (CDC-NCM) and serves its config page at
**http://10.7.7.107/**. The page live-lists nearby BLE advertisers with name +
RSSI so you can pick your device instead of typing a MAC address.

If `10.7.7.x` clashes with your LAN, Settings lets you switch the config page to
one of a few fixed addresses (`172.31.7.107`, `192.168.137.107`, `10.77.77.107`).
The choice is saved to flash; **unplug and replug** the dongle for it to take
effect, then browse to the new address.

> **http://picowake.local/** may also work, but mDNS/Bonjour resolution depends
> on your OS and isn't reliable everywhere — **use the IP address** if `.local`
> doesn't resolve.

## What works / what doesn't

- ✅ Devices that advertise over **Bluetooth LE**: Xbox Series controllers,
  most BLE mice/keyboards/earbuds, 8BitDo pads in BLE modes, etc.
- ❌ **Bluetooth Classic**-only devices (DualSense, DualShock 4, Switch Pro):
  these don't advertise when reconnecting — they page the PC directly, which
  a passive observer cannot see. That's a protocol limitation, not a bug.
- ⚠️ Devices with **rotating random MAC addresses** (phones, some earbuds)
  won't match reliably.
- Wake-from-soft-off (S5) requires a motherboard that keeps USB powered in
  S5 with keyboard wake enabled (check BIOS: "wake on USB keyboard", ErP off).

## Supported boards

| Board | Build flag | UF2 |
|---|---|---|
| Pico 2 W (default) | — | `pc-wake-dongle.uf2` |
| Pico W | `-DPICO_W_BUILD=ON` | `pc-wake-dongle-picow.uf2` |
| Waveshare RP2350B-Plus-W | `-DWAVESHARE_RP2350B_PLUS_W_BUILD=ON` | `pc-wake-dongle-waveshare.uf2` |

## Getting started

1. Grab the `.uf2` for your board from [Releases](../../releases), or build it
   yourself (below).
2. Hold BOOTSEL, plug the Pico in, drop the `.uf2` onto the mounted drive.
3. Open **http://10.7.7.107/** (the dongle shows up as a network adapter; no
   internet traffic ever routes through it).
4. Power-cycle your gamepad and click its entry in the device list. It's
   added with wake already enabled and saved automatically — no Save button.

LED: solid = host awake; blinking = scanning (host asleep or config page
open); off = idle.

## Build instructions

### Windows (one command)

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1            # Pico 2 W
powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 -Variant picow
powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 -Variant debug
```

The script installs everything it needs (CMake, Ninja, ARM GCC, Pico SDK) and
drops the `.uf2` next to the script and on your Desktop.

### Manual

Requires the Pico SDK (2.2.0, TinyUSB ≥ 0.20) and ARM GCC:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Options: `ENABLE_SERIAL` (USB serial console), `ENABLE_VERBOSE`, `WAKE_DEBUG`
(wake state machine tracing).

## How it's built

- TinyUSB composite device: HID boot keyboard + CDC-NCM (+ CDC serial in
  debug builds)
- BTstack (LE-only) passive scanning on the CYW43
- lwIP (NO_SYS) over NCM with a tiny DHCP server, mDNS responder and httpd
  serving the config page from firmware — no internet, no drivers needed
- Config (device list, global/LED toggles, selected subnet) stored in the last
  flash sector

## Lineage

Forked from
[Kirkland-Pickles/DS5Dongle-windows-htpc](https://github.com/Kirkland-Pickles/DS5Dongle-windows-htpc),
itself a fork of [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle) (a
DualSense wireless adapter). The DualSense bridging, audio and gamepad code
was removed; the USB remote-wakeup state machine (with its Windows/Linux
quirk handling) and the BLE scan-to-wake feature were kept and built upon.

## License

Licensed under the **GNU General Public License v3.0** — see [LICENSE](./LICENSE).

This project derives from MIT-licensed code (see Lineage above); the original
MIT notice is preserved in [LICENSE-MIT](./LICENSE-MIT) as that license requires.
