# esp32-s3-nut-node

ESP32-S3 firmware that turns a USB UPS into a network-accessible NUT (Network UPS Tools) node — no Linux server required.

The ESP32 connects to the UPS via USB HID, decodes the interrupt stream and feature reports, and speaks the NUT protocol natively over TCP. Any standard NUT client (`upsc`, `upsmon`, Home Assistant) can query it directly.

**Current version:** v15.8 | **Status:** Production

---

## Features

- **Native NUT server** on tcp/3493 — compatible with any NUT 2.x client
- **USB HID driver** — reads UPS reports directly, no `apcupsd`, no OS, no Linux
- **Multi-vendor support** — APC Back-UPS, CyberPower, and more (see [Compatible UPS List](docs/COMPATIBLE-UPS.md))
- **GET_REPORT polling** — retrieves Feature reports for devices that don't broadcast voltage on interrupt-IN (APC)
- **Device database** — VID:PID lookup with per-device quirk flags
- **HTTP portal** on tcp/80
  - Live dashboard with AJAX polling (5s refresh — no page reload)
  - Config form: WiFi SSID/password, NUT identity, portal password
  - `/status` JSON endpoint — unauthenticated, for scripts and monitoring
  - `/compat` — Compatible UPS list
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

## Confirmed Working UPS Devices

| Device | VID:PID | Status |
|--------|---------|--------|
| APC Back-UPS XS 1500M | 051D:0002 | ✅ Confirmed |
| APC Back-UPS BR1000G | 051D:0002 | ✅ Confirmed |
| CyberPower ST Series (CP550HG etc.) | 0764:0501 | ✅ Confirmed |

See [docs/COMPATIBLE-UPS.md](docs/COMPATIBLE-UPS.md) for the full compatible device list.

---

## Quick Start

### Requirements
- ESP-IDF v5.3.1
- ESP32-S3 board with USB OTG support
- UPS with USB HID interface

### Build and Flash
```powershell
# From src/current/
idf.py build flash monitor -p COM3
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

## NUT Variables Published

| Variable | Source | Notes |
|----------|--------|-------|
| `ups.status` | interrupt-IN | OL / OB / CHRG / DISCHRG / LB |
| `ups.model` | USB string descriptor | |
| `ups.mfr` | USB string descriptor | |
| `ups.firmware` | USB string descriptor | |
| `battery.charge` | interrupt-IN | % |
| `battery.runtime` | interrupt-IN | seconds |
| `battery.voltage` | interrupt-IN | V (CyberPower only) |
| `input.voltage` | GET_REPORT rid=0x17 | V (APC only) |
| `output.voltage` | GET_REPORT rid=0x17 | V (APC only) |
| `device.type` | static | "ups" |

---

## Source File Overview

```
src/current/main/
├── main.c              — App init, task orchestration
├── wifi_mgr.c/h        — WiFi STA + SoftAP
├── cfg_store.c/h       — NVS credential storage
├── http_portal.c/h     — Dashboard + config portal
├── nut_server.c/h      — NUT TCP server (port 3493)
├── ups_state.c/h       — Shared UPS state struct
├── ups_usb_hid.c/h     — USB Host + interrupt-IN reader
├── ups_hid_desc.c/h    — HID Report Descriptor parser
├── ups_hid_parser.c/h  — Field decoder (charge, runtime, voltage, status)
├── ups_device_db.c/h   — VID:PID device database + quirk flags
└── ups_get_report.c/h  — GET_REPORT polling (Feature reports via USB control transfer)
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/HISTORY.md](docs/HISTORY.md) | Full development history — v1 to v15.8 |
| [docs/COMPATIBLE-UPS.md](docs/COMPATIBLE-UPS.md) | Compatible UPS quick reference |
| [docs/usbhid-ups-compat.md](docs/usbhid-ups-compat.md) | Full HID Usage ID + NUT variable reference |
| [docs/TODO.md](docs/TODO.md) | Pending tasks |
| [docs/REVERT-INDEX.md](docs/REVERT-INDEX.md) | Stable revert points |
| [CHANGELOG.md](CHANGELOG.md) | Version changelog |

---

## License

MIT
