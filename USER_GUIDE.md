# User Guide: Wake-on-LAN fallback (S5 / soft-off)

This page covers the **Wake-on-LAN (WOL)** fallback in detail. For everything
else (flashing, the BLE device list, the basic config page), see the main
[README](README.md).

## Why this exists

Waking a PC from **S3 (sleep)** over USB-HID remote wakeup is a near-universal
feature — that's what the dongle does by default, with no setup. Waking from
**S5 (soft-off / fully shut down)** over USB-HID is not: it only works on a
handful of motherboards (mainly ASRock and MSI). **Wake-on-LAN from S5** is
supported much more broadly. So when "Enable WOL" is turned on, the dongle
*also* sends a WOL magic packet over its own Wi-Fi connection whenever it
would otherwise try to wake the PC — covering boards where the USB path
can't reach S5 at all.

The dongle can't reliably tell S3 apart from S5 over USB, so it always fires
both wake paths together. A stray WOL packet while the PC is merely asleep
in S3 is harmless.

## Requirements

Your PC must be wired to the same LAN by Ethernet, with NIC Wake-on-LAN
enabled in **both** BIOS and OS:

- **BIOS/UEFI**: enable "Wake on LAN" / "Power On by PCIE" (naming varies by
  vendor), and **disable ErP/EuP** ("deep power off") — ErP cuts power to
  the NIC in S5, which would otherwise also leave the dongle without a way
  to wake it. ErP/EuP often defaults to *enabled* and needs an explicit
  override.
- **OS** (Windows): Device Manager → your network adapter → Power Management
  tab → "Allow this device to wake the computer". Also check the adapter's
  Advanced settings for a "Wake on Magic Packet" option and enable it.
- The dongle itself must stay powered while the PC is off — this is normally
  automatic if it's plugged into a USB port that supplies standby power.

## Setup

Open the dongle's config page (default **http://10.7.7.107/**) and scroll to
**"Wake-on-LAN from Shutdown (S5, soft-off)"**.

![Config page](assets/webconfig.png)

1. Tick **"Enable wake-on-LAN"**.
2. Under **Home Wi-Fi network**, click **Scan** and pick your SSID from the
   dropdown (2.4 GHz only — the CYW43 radio doesn't support 5 GHz), or type
   the SSID manually if it doesn't show up. Enter the password and click
   **Save Wi-Fi**.
   - The status line below updates to `connecting…`, then
     `connected <ip>` once it's joined. **Joining can take several seconds and
     a few internal retries** — see [Why Wi-Fi can be slow to
     connect](#why-wi-fi-can-be-slow-to-connect) below; this is expected, not a
     fault. If you see `auth failed`, re-check the password; `network not found`
     means it tried for ~30 s without finding the AP — usually out of range or
     5 GHz-only.
3. Under **Target PC**, enter the PC's **IP address** and click
   **Resolve MAC** — the dongle ARPs for it on your LAN and fills in the MAC
   address field. (This only works while the PC is awake, since a sleeping
   or shut-down PC won't answer ARP.) Click **Save target**.
4. Power off the PC fully (S5) and test by powering on the paired BLE device.

## Why Wi-Fi can be slow to connect

The dongle has **one wireless radio** (the CYW43 chip), and it has to share it
between **Bluetooth** — which is constantly scanning for your paired device so it
can trigger the wake — and **Wi-Fi**, which it needs for the Wake-on-LAN path.
The two cannot both use the radio at the same instant, so the chip time-slices
between them.

The practical effect is that **joining your Wi-Fi can take a few seconds and
several internal retries**, and occasionally a join attempt fails and is
retried. This is inherent to a single-radio device that must keep listening for
the wake trigger; the firmware already:

- retries the join automatically (it does *not* give up after one failure),
- pauses the config page's active Bluetooth scan while it's connecting,
- runs the Wi-Fi radio with power-saving disabled, and
- is built for full transmit power on 2.4 GHz channels 1–11.

There is no setting that removes the retries entirely — it's a hardware
trade-off, not a misconfiguration. What genuinely helps:

- **Keep the dongle close to the router** (or on a USB extension cable away from
  the PC case, which shields the antenna). A strong 2.4 GHz signal is the single
  biggest factor.
- **Use a 2.4 GHz channel in the 1–11 range** on your router (most are by
  default). Channels 12–13 are low-power on any client and make this worse.
- Once connected, it **stays** connected; the slowness is only at join time.

## Troubleshooting

- **Takes a few tries / a few seconds to connect**: expected — see
  [Why Wi-Fi can be slow to connect](#why-wi-fi-can-be-slow-to-connect). The
  dongle retries on its own and connects within a few seconds. Move it closer to
  the router if it's persistently slow.
- **WOL sent but PC doesn't wake**: double-check ErP/EuP is off and "Wake on
  Magic Packet" is enabled in the OS adapter settings — these are the two
  most commonly missed settings.
- **MAC won't resolve**: make sure the PC is awake and on the same subnet as
  the dongle's Wi-Fi connection when you click Resolve.
