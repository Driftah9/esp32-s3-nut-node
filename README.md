# esp32-s3-nut-node

ESP32-S3 firmware that turns an APC UPS into a network-accessible NUT (Network UPS Tools) node — no Linux server required.

The ESP32 connects to the UPS via USB HID, decodes the interrupt stream, and speaks the NUT protocol natively over TCP. Any standard NUT client (`upsc`, `upsmon`, Home Assistant) can query it directly.

---

## Features

- **Native NUT server** on tcp/3493 — compatible with any NUT 2.x client
- **USB HID driver** — reads APC UPS reports directly, no `apcupsd`, no OS
- **HTTP config portal** on tcp/80
  - Live dashboard with AJAX polling (Status, Charge, IP update every 5s)
  - Config form: Wi-Fi, Soft AP, NUT identity, portal password
  - HTTP Basic Auth — default password `upsmon`, prompted to change on first login
  - `/status` endpoint — unauthenticated JSON for scripts and monitoring
- **SoftAP provisioning** — broadcasts setup AP on first boot or when STA disconnects
- **Config persistence** — all settings stored in NVS (survives power cycle)

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3 (tested on generic devkit) |
| UPS | APC Back-UPS XS 1500M (USB HID, VID:PID 051d:0002) |
| Connection | USB OTG (ESP32-S3 acts as USB host) |
| Power | USB-C (devkit powered separately from UPS USB) |

Other APC UPS models with HID interface should work — the parser has model-aware decode for XS1500M with generic fallback.

---

## Confirmed NUT variables

```
battery.charge          battery.charge.low      battery.type
battery.voltage         input.utility.present   input.voltage
ups.status              ups.flags               ups.type
ups.firmware            ups.load                device.mfr
device.model            device.serial           driver.name
driver.version          ups.vendorid            ups.productid
```

`ups.status` values: `OL`, `OL CHRG`, `OB DISCHRG`, `OB DISCHRG LB`

---

## Build

Requires ESP-IDF v5.3.x.

```powershell
# Windows (PowerShell)
idf.py build flash -p COM3
idf.py monitor -p COM3
```

```bash
# Linux/macOS
idf.py build flash -p /dev/ttyUSB0
idf.py monitor -p /dev/ttyUSB0
```

First boot: connect to the `ESP32-UPS-SETUP-XXXXXX` SoftAP, navigate to `http://192.168.4.1/config`, configure Wi-Fi credentials.

---

## First boot setup

1. Power the ESP32 (UPS USB cable plugged into OTG port)
2. Connect to Wi-Fi SSID: `ESP32-UPS-SETUP-XXXXXX`
3. Open `http://192.168.4.1/config`
4. Login: `admin` / `upsmon` (default — you will be prompted to change this)
5. Set your Wi-Fi SSID and password, save
6. Device connects to your network, SoftAP turns off
7. Find the device IP at `http://192.168.4.1/status` or via your router

---

## HTTP portal

| Route | Auth | Description |
|-------|------|-------------|
| `GET /` | ✅ | Live dashboard |
| `GET /config` | ✅ | Configuration form |
| `POST /save` | ✅ | Save config to NVS |
| `GET /status` | ❌ | JSON snapshot (open) |
| `GET /reboot` | ✅ | Reboot device |

Auth: HTTP Basic, username `admin`, password set in portal.

---

## Source layout

```
main/
  main.c              — app entry, task init
  cfg_store.c/h       — NVS config, defaults, password helpers
  wifi_mgr.c/h        — STA + SoftAP lifecycle, reconnect timer
  http_portal.c/h     — HTTP server, all routes, AJAX dashboard
  nut_server.c/h      — NUT protocol server (tcp/3493)
  ups_state.c/h       — Shared UPS state, mutex-protected snapshot
  ups_usb_hid.c/h     — USB HID host, interrupt IN reader
  ups_hid_parser.c/h  — Report decode, model-aware (XS1500M + generic)
CMakeLists.txt
sdkconfig               — saved menuconfig state
```

---

## Using with NUT clients

```bash
# Direct query
upsc ups@<device-ip>

# Via NUT hub (recommended for multi-client)
# See docs/linux/M9-nut-hub-setup.md
upsc xs1500m@<hub-ip>
```

Home Assistant: Settings → Integrations → NUT → host: `<device-ip>`, port: `3493`

---

## License

MIT
