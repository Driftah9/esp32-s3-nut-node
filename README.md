# ESP32-S3 UPS NUT Node

ESP32-S3 firmware that turns a USB UPS into a network-accessible NUT (Network UPS Tools) node — no Linux server required.

The ESP32 connects to the UPS via USB HID, decodes the interrupt stream and feature reports, and speaks the NUT protocol natively over TCP. Any standard NUT client (`upsc`, `upsmon`, Home Assistant) can query it directly.

**Current version:** v15.13 | **Status:** Production

---

> 💛 **If this project saves you a Raspberry Pi, consider buying me a coffee!**  
> [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/B0B2DKG8N)

---

## Features

- **Native NUT server** on tcp/3493 — compatible with any NUT 2.x client
- **USB HID driver** — reads UPS reports directly, no `apcupsd`, no OS, no Linux
- **Graceful USB hot-unplug** — safe disconnect and reconnect without panic or reboot
- **Multi-vendor support** — APC Back-UPS, CyberPower, and more (see [Compatible UPS List](docs/confirmed-ups.md))
- **GET_REPORT polling** — retrieves Feature reports for devices that don't broadcast voltage on interrupt-IN (APC)
- **Device database** — VID:PID lookup with per-device quirk flags and NUT static variables
- **Full NUT variable parity** — serves battery.voltage.nominal, battery.runtime.low, battery.charge.warning, input.voltage.nominal, ups.type, ups.test.result, ups.delay.shutdown, ups.timer.reboot per device
- **HTTP portal** on tcp/80
  - Live dashboard with AJAX polling (5s refresh — no page reload)
  - **NUT Variables lightbox** — click to see the full `upsc`-style variable list live from the device
  - Config form: WiFi SSID/password, NUT identity, portal password
  - `/status` JSON endpoint — unauthenticated, for scripts and monitoring
  - `/compat` — Compatible UPS list, expandable by vendor
- **SoftAP provisioning** — broadcasts setup AP on first boot or when STA disconnects
- **Config persistence** — all settings stored in NVS (survives power cycle)

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | Hosyond ESP32-S3-WROOM-1 N16R8 devkit (16MB flash, 8MB PSRAM) |
| UPS tested | APC Back-UPS XS 1500M, APC Back-UPS BR1000G, CyberPower ST Series |
| UPS connection | USB-A → OTG port on ESP32-S3 (ESP32 acts as USB host) |
| Power | USB-C port (powered independently from UPS USB) |

---

## Confirmed Compatible UPS Devices

| Device | VID:PID | Status |
|--------|---------|--------|
| APC Back-UPS XS 1500M | 051D:0002 | ✅ Confirmed |
| APC Back-UPS BR1000G | 051D:0002 | ✅ Confirmed |
| CyberPower CP550HG / SX550G | 0764:0501 | ✅ Confirmed |

Full compatibility list: [`docs/confirmed-ups.md`](docs/confirmed-ups.md)

**Your UPS not listed?** If it connects and reports data — [submit a compatibility report](https://github.com/Driftah9/esp32-s3-nut-node/issues/new?template=ups-compatibility-report.yml) and get it added to the confirmed list.  
You'll need: UPS brand/model, USB VID:PID (shown in the portal), and the first 200 lines of serial log from a fresh boot.

---

## Quick Start

### Requirements
- ESP-IDF v5.3.1
- ESP32-S3 board with USB OTG support
- UPS with USB HID interface

### Build and Flash
```powershell
# From src/current/ in the ESP-IDF PowerShell shell
idf.py build
idf.py flash -p COM3
```

### First Boot
1. ESP32 broadcasts WiFi AP: `ESP32-UPS-SETUP-XXXXXX`
2. Connect and browse to `http://192.168.4.1/config`
3. Enter your WiFi credentials → Save → ESP32 reboots onto your LAN
4. Browse to `http://<ESP32-IP>/` for the dashboard

### NUT Client Setup (Home Assistant)
```
Settings → Devices & Services → Add Integration → Network UPS Tools
Host: <ESP32-IP>   Port: 3493   UPS name: ups
```

---

## Web Portal

| Endpoint | Description |
|----------|-------------|
| `/` | Live dashboard — status, charge, runtime, voltages, NUT Variables lightbox |
| `/config` | WiFi, NUT identity, portal password |
| `/compat` | Compatible UPS list — expandable by vendor |
| `/status` | JSON snapshot — unauthenticated, for scripts and monitoring |

---

## NUT Variables Published

| Variable | Source | Notes |
|----------|--------|-------|
| `ups.status` | interrupt-IN | OL / OB / CHRG / DISCHRG / LB |
| `ups.model` | USB string descriptor | |
| `ups.mfr` | USB string descriptor | |
| `ups.firmware` | USB string descriptor | |
| `ups.type` | device DB | line-interactive / online / standby |
| `ups.test.result` | static | No test initiated |
| `ups.delay.shutdown` | static | 20 |
| `ups.delay.start` | static | 30 |
| `ups.timer.reboot` | static | -1 |
| `ups.timer.shutdown` | static | -1 |
| `battery.charge` | interrupt-IN | % |
| `battery.charge.low` | device DB | % threshold |
| `battery.charge.warning` | device DB | % threshold |
| `battery.runtime` | interrupt-IN | seconds |
| `battery.runtime.low` | device DB | seconds threshold |
| `battery.voltage` | interrupt-IN | V (CyberPower) |
| `battery.voltage.nominal` | device DB | V |
| `battery.type` | static | PbAc |
| `input.voltage` | GET_REPORT | V (APC) |
| `input.voltage.nominal` | device DB | V |
| `output.voltage` | GET_REPORT | V (APC) |
| `device.mfr` | USB descriptor | |
| `device.model` | USB descriptor | |
| `device.serial` | USB descriptor | |
| `device.type` | static | ups |
| `driver.name` | static | esp32-nut-hid |
| `driver.version` | static | 15.13 |

---

## Source File Overview

```
src/current/main/
├── main.c                — App init, task orchestration
├── wifi_mgr.c/h          — WiFi STA + SoftAP
├── cfg_store.c/h         — NVS credential storage
├── http_portal.c/h       — HTTP routing, /status JSON, auth
├── http_dashboard.c/h    — Live dashboard with AJAX + NUT lightbox
├── http_config_page.c/h  — Config form render + form parsing
├── http_compat.c/h       — Compatible UPS list page
├── http_portal_css.h     — Shared portal CSS
├── nut_server.c/h        — NUT TCP server (port 3493)
├── ups_state.c/h         — Shared UPS state struct
├── ups_usb_hid.c/h       — USB Host + interrupt-IN reader + hot-unplug handling
├── ups_hid_desc.c/h      — HID Report Descriptor parser
├── ups_hid_parser.c/h    — Field decoder (charge, runtime, voltage, status)
├── ups_device_db.c/h     — VID:PID device database, quirk flags, NUT static fields
└── ups_get_report.c/h    — GET_REPORT polling (Feature reports via USB control transfer)
```

---

## Project Structure

```
src/current/          — Active firmware source (ESP-IDF component)
docs/                 — Hardware notes, HID reference, compatibility
docs/confirmed-ups.md — Community-confirmed compatible UPS devices
scripts/              — Utility scripts (submission validation, etc.)
.github/              — Issue templates and automation workflows
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/HISTORY.md](docs/HISTORY.md) | Full development history |
| [docs/confirmed-ups.md](docs/confirmed-ups.md) | Community-confirmed compatible devices |
| [docs/COMPATIBLE-UPS.md](docs/COMPATIBLE-UPS.md) | Compatible UPS quick reference |
| [docs/usbhid-ups-compat.md](docs/usbhid-ups-compat.md) | Full HID Usage ID + NUT variable reference |
| [docs/REVERT-INDEX.md](docs/REVERT-INDEX.md) | Stable revert points |

---

## Have a UPS That Works?

If your UPS connects and reports data correctly — **please submit a compatibility report!**  
It helps other users know what works, and your device gets added to the confirmed list.

👉 [Submit a UPS Compatibility Report](https://github.com/Driftah9/esp32-s3-nut-node/issues/new?template=ups-compatibility-report.yml)

What you'll need:
- UPS brand and model
- USB VID:PID (shown in the web portal or serial log)
- First 200 lines of serial log from a fresh boot with the UPS connected
- Firmware version (shown in portal subtitle or boot banner)

## Found a Bug?

👉 [Open a Bug Report](https://github.com/Driftah9/esp32-s3-nut-node/issues/new?template=bug_report.yml)

---

## License

MIT

---

> 💛 Like the project? [Buy me a coffee on Ko-fi](https://ko-fi.com/B0B2DKG8N)
