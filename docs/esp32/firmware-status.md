# ESP32-S3 NUT Node — Firmware Status
# v14.25 R10 — COMPLETE
# Updated: 2026-03-07

## Device
- Hardware: ESP32-S3
- UPS: APC Back-UPS XS 1500M (USB HID)
- IP: 10.0.0.190
- NUT port: tcp/3493

## Current firmware: v14.25 R10

### Flash command
```powershell
cd D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current
idf.py build flash -p COM3
idf.py monitor -p COM3
```

---

## What the firmware does

### NUT server (tcp/3493)
- Speaks NUT protocol natively — compatible with any NUT client (upsc, upsmon, HA)
- Exposes 17 variables from XS1500M HID interrupt stream
- Single-client design (closes after LIST VAR) — sufficient for polling clients
- No banner mode (upsd-compatible)
- 5s recv timeout on NUT connections

### HTTP portal (tcp/80)
- `GET /`        — Live dashboard. AJAX polls /status every 5s, updates
                   Status, Charge, and STA IP cells in-place. No page reload.
                   Polling stops when tab is closed.
- `GET /config`  — Configuration form (Wi-Fi, AP, NUT identity, portal password)
- `POST /save`   — Saves config to NVS, shows confirmation
- `GET /status`  — Unauthenticated JSON snapshot (for scripts, HA, monitoring)
- `GET /reboot`  — Reboots device (with confirm dialog)
- HTTP Basic Auth: username=`admin`, password=`portal_pass`
- Default password: `upsmon` — warning shown on dashboard and config until changed
- `/status` is always open (no auth required)

### Wi-Fi
- Connects to STA (home network) on boot
- SoftAP (`ESP32-UPS-SETUP-XXXXXX`) active during setup
- AP disabled ~2s after STA gets IP
- AP re-enables after 60s if STA disconnects
- AP disables again on STA reconnect

### Config persistence
- All settings stored in NVS (survives reboot/power cycle)
- Keys: sta_ssid, sta_pass, ap_ssid, ap_pass, ups_name, nut_user, nut_pass, portal_pass

---

## Confirmed NUT variables — XS1500M

| Variable | Example value |
|----------|---------------|
| battery.charge | 97 |
| battery.charge.low | 20 |
| battery.type | PbAc |
| battery.voltage | 34.920 |
| battery.runtime | - (absent, deferred) |
| input.utility.present | 1 |
| input.voltage | 120.0 |
| ups.status | OL |
| ups.flags | 0x0000000D |
| ups.type | line-interactive |
| ups.firmware | 947.d10 |
| ups.load | 13 |
| device.mfr | American Power Conversion |
| device.model | Back-UPS XS 1500M FW:947.d10 .D USB FW:d10 |
| device.serial | 0B2051N02171 |
| driver.name | esp32-nut-hid |
| driver.version | 14.25 |
| ups.vendorid | 051d |
| ups.productid | 0002 |

---

## REVERT index

| Tag | Version | Description |
|-----|---------|-------------|
| REVERT-0007 | v14.18 | NUT recv timeout + no-banner |
| REVERT-0008 | v14.24 | XS1500M model-aware decode |
| REVERT-0009 | v14.25 R10 | Current stable — portal + auth + AJAX |

### v14.25 patch history
| Rev | Fix |
|-----|-----|
| R4 | Heap-allocated rx/page buffers — fixed stack overflow crash |
| R5 | socket_close_graceful() — fixed Chrome ERR_CONNECTION_RESET |
| R8 | Zero-CSS plain HTML — fixed blank page (CSS buffer truncation) |
| R9 | Default password "upsmon" + change-password warning |
| R10 | AJAX polling on dashboard — live Status/Charge/IP updates every 5s |

---

## Known deferred items (not blocking)

- **battery.runtime** — not in XS1500M HID interrupt stream. Would need
  GET_REPORT control transfer polling (architectural change). Low value.
- **output.voltage** — report 0x14 always zero on mains. BR1000G on live
  infra, cannot test. Closed.
- **driver.version shows 14.24** — cosmetic only, one-liner fix when needed.
- **Portal CSS** — plain HTML in use. Can add /style.css endpoint later
  without touching page buffer sizing.
- **Multi-client NUT** — single-client sufficient for current use.
  Revisit if upsmon + upsc need to run simultaneously.

---

## Source file inventory

```
src/current/main/
  main.c              — app entry, task init, version string
  cfg_store.c / .h    — NVS config load/save, defaults, is_default_pass()
  wifi_mgr.c / .h     — STA + SoftAP lifecycle, 60s reconnect timer
  http_portal.c / .h  — HTTP server, all routes, AJAX dashboard
  nut_server.c / .h   — NUT protocol server (tcp/3493)
  ups_state.c / .h    — Shared UPS state, mutex-protected snapshot
  ups_usb_hid.c / .h  — USB HID host, interrupt IN reader
  ups_hid_parser.c / .h — Report decode, model-aware (XS1500M, BR1000G, generic)
  CMakeLists.txt
```
