# ESP32-S3 NUT Node — Development History
**Project:** esp32-s3-nut-node  
**Hardware:** ESP32-S3 (16MB flash, 8MB PSRAM) + USB OTG  
**Current Version:** v15.13 (2026-03-15)  
**GitHub:** https://github.com/Driftah9/esp32-s3-nut-node

---

## Overview

This document chronicles the full evolution of the ESP32-S3 UPS NUT Node firmware,
from a single-file HTTP stub to a multi-module, multi-UPS NUT server with live USB HID
decoding, GET_REPORT polling, and Home Assistant integration.

---

## Phase 1 — Single File Monolith (v1–v13)

### v1–v9 — Concept and Basic HTTP

The project started as a proof-of-concept to turn an ESP32-S3 into a UPS monitoring
node. Early versions (v1–v9) were single `main.c` files that:

- Connected to WiFi (hardcoded SSID/password)
- Exposed a basic HTTP page showing dummy UPS status
- Had no real USB communication — values were hardcoded
- Used `esp_http_server` for a simple status endpoint

The core question being answered: *can an ESP32-S3 act as a NUT server that Home
Assistant can poll?*

### v10 — HTTP Status Endpoint

- Added `/status` HTTP endpoint returning JSON
- Still dummy data, but the HA NUT integration can now connect and receive values
- First successful HA → ESP32 → fake UPS data path confirmed

### v11 — NUT TCP Server

- Replaced HTTP-only approach with a proper NUT protocol server on tcp/3493
- Implemented the core NUT commands: `LIST UPS`, `LIST VAR`, `GET VAR`
- HA NUT integration now connects via the real NUT protocol
- UPS data still hardcoded (battery.charge=100, ups.status=OL)

### v12 — LAN Mode + WiFi STA

- Added NVS-backed WiFi credentials storage
- STA mode connects to home WiFi; SoftAP fallback for initial config
- First version to get a real IP on the LAN (10.0.0.190)
- Portal at http://10.0.0.190/config for WiFi setup

### v13 — USB Autodiscovery Skeleton

- Added USB Host library initialization
- USB device detection scaffolding (no real HID decode yet)
- AP password validation, URI escaping fixes
- Multiple sub-revisions (v13.1–v13.12) fixing WiFi edge cases

---

## Phase 2 — Modular Architecture (v14)

### v14.0 — Modular Refactor

Major architectural split. `main.c` broken into modules:

| Module | Responsibility |
|--------|---------------|
| `main.c` | App init, task orchestration |
| `wifi_mgr.c` | WiFi STA + SoftAP management |
| `cfg_store.c` | NVS credentials storage |
| `http_portal.c` | Dashboard + config web portal |
| `nut_server.c` | NUT TCP server on port 3493 |
| `ups_state.c` | Shared UPS state struct |
| `ups_usb_hid.c` | USB Host + HID interrupt-IN reader |

### v14.1–v14.7 — USB HID Descriptor Parser

This was the hardest phase. The ESP32 USB Host library gives raw bytes. Turning them
into UPS values required a full HID Report Descriptor parser:

- **v14.1–v14.3:** Initial HID descriptor parser (`ups_hid_parser.c`) — parse usage pages,
  report IDs, bit offsets
- **v14.4:** Output voltage probe — discovered CyberPower sends voltages on report IDs
  not in the standard descriptor
- **v14.5:** Descriptor map extraction — separate `ups_hid_desc.c` for the field cache
- **v14.6:** APC vendor page normalization — APC uses 0xFF84/0xFF85 vendor pages which
  must be remapped to standard 0x84/0x85 to match NUT's usage table
- **v14.7:** Stable baseline — battery.charge, battery.runtime decoding working on
  CyberPower (VID=0764 PID=0501). First real live values from USB.
- **v14.8:** USB reattach/reconnect fixes — device would silently stop sending after
  hot-plug events

### v14.25 — WiFi + NVS Stable Baseline

Long-term stable release of the WiFi/NVS/portal stack. This version of `cfg_store.c`,
`wifi_mgr.c` has been unchanged through all subsequent versions.

---

## Phase 3 — Multi-UPS + APC Support (v15)

### v15.0–v15.2 — Device Database

- Added `ups_device_db.c` — VID:PID lookup table with per-device quirk flags
- Quirk system: `QUIRK_VENDOR_PAGE_REMAP`, `QUIRK_NEEDS_GET_REPORT`,
  `QUIRK_DIRECT_DECODE`
- APC Back-UPS (PID=0002) identified and flagged for GET_REPORT polling
- CyberPower (PID=0501) confirmed on direct-decode path

### v15.3 — CyberPower Full Decode + AJAX Dashboard

- CyberPower battery.charge, battery.runtime, battery.voltage all decoding from
  interrupt-IN reports
- Dashboard switched to full AJAX (5s polling) — no page reload needed
- `/status` JSON endpoint expanded with all live fields
- `derive_ups_status()` logic corrected — OL/OB/CHRG/DISCHRG all working
- First version confirmed working with **two different UPS brands simultaneously**
  (CyberPower ST Series + APC Back-UPS)

### v15.4–v15.6 — APC HID Descriptor Investigation

Deep investigation into why the APC Back-UPS reports no battery/voltage data from
the interrupt-IN stream:

- APC XS 1500M descriptor: 1049 bytes, 24 fields, 16 report IDs
- Interrupt-IN reports carry charging/discharging flags and battery charge — but NOT
  input/output voltage
- Voltage is only available via USB Control Transfer GET_REPORT (Feature reports)
- All rids 0x20–0x29, 0x40–0x42 probed — found charge (rid=0x22=100%), runtime
  (rid=0x23=36000s), but NOT voltage
- APC `/compat` page added showing confirmed and unconfirmed device list

### v15.7 — Architecture Cleanup

- `ups_hid_desc.c/h` stabilized
- `ups_hid_parser.c` — APC decode mode finalized (direct + standard combined)
- Dynamic `/compat` page — Compatible UPS List served from device DB
- sdkconfig.defaults locked down for 16MB flash + 80MHz

### v15.8 — GET_REPORT Voltage Polling (Production)

The final major feature. Root cause of GET_REPORT not working was found and fixed:

**Root cause:** `usb_host_client_handle_events(s_client, portMAX_DELAY)` in
`ups_usb_hid.c` blocked forever — the GET_REPORT service queue was never drained.

**Fix:** Changed timeout to `pdMS_TO_TICKS(10)` and added
`ups_get_report_service_queue()` call on every loop iteration.

**Architecture:** Single-owner queue pattern — only `usb_client_task` ever issues
USB control transfers. A timer task posts RID requests to a FreeRTOS queue; the USB
task drains one per 10ms loop.

**APC Feature Report probe results** (XS 1500M, PID=0002):
- rid=0x17 → `[17 78 00]` → uint16 LE = 120 → **input/output voltage = 120V** ✅
- Battery voltage (rids 0x82–0x88): ALL STALL — not available on this firmware

**Result:** `input.voltage = output.voltage = 120V` now live in dashboard and NUT.

---

## Confirmed Working Devices at v15.13

| Device | VID:PID | Decode Path | Data Available |
|--------|---------|-------------|----------------|
| APC Back-UPS XS 1500M | 051D:0002 | interrupt-IN + GET_REPORT | charge, runtime, status, input/output voltage |
| APC Back-UPS BR1000G | 051D:0002 | interrupt-IN + GET_REPORT | charge, runtime, status, input/output voltage |
| CyberPower ST Series | 0764:0501 | interrupt-IN (direct decode) | charge, runtime, battery voltage, status |

---

## Architecture at v15.8

```
app_main()
├── nvs_flash_init()
├── wifi_mgr_init()          ← STA + SoftAP, NVS credentials
├── ups_state_init()         ← shared state struct
├── ups_usb_hid_start()      ← USB Host task
│   ├── usb_host_install()
│   ├── usb_client_task()    ← event loop (10ms tick)
│   │   ├── usb_host_client_handle_events()
│   │   ├── ups_get_report_service_queue()   ← drains GET_REPORT requests
│   │   └── interrupt_in_callback()          ← HID report decode
│   └── ups_get_report timer task            ← posts rid=0x17 every 30s
├── http_portal_start()      ← dashboard on :80
└── nut_server_start()       ← NUT on tcp/3493
```

---

## Key Technical Lessons

1. **USB Host is single-threaded** — never call `usb_host_transfer_submit_control()`
   from any task except the one that called `usb_host_client_handle_events()`.

2. **APC uses vendor HID pages** — 0xFF84 and 0xFF85 must be normalized to 0x84/0x85
   to match NUT's standard usage table.

3. **APC battery voltage is not accessible via USB HID** on consumer Back-UPS firmware
   (PID=0002). All high report IDs STALL. This matches NUT's behavior on the same hardware.

4. **`portMAX_DELAY` in a USB event loop is fatal** — it prevents any other USB work
   (control transfers, queue service) from running. Always use a bounded timeout.

5. **CyberPower PID=0501 has a descriptor bug** — output voltage `LogMax` is set to
   the HighVoltageTransfer max. NUT's `cps-hid.c` patches this at runtime; our parser
   needs the same fix for correct voltage scaling.

6. **HID descriptor parsing requires context tracking** — the same Usage ID (e.g.,
   0x0030 = Voltage) means different NUT variables depending on which Collection it
   appears in (Input vs Output vs PowerSummary).

7. **USB hotplug requires careful flag ordering** — the `s_cleanup_pending` flag must
   be set BEFORE `s_dev_gone` in the client event callback to prevent `intr_in_cb`
   from resubmitting transfers during cleanup. `portMAX_DELAY` in the USB lib task
   also prevents cleanup — use a bounded 50ms timeout with a 1-tick yield.

---

## Phase 4 — NUT Parity + Portal Enhancement (v15.9–v15.13)

### v15.9–v15.11 — Portal Refactor + Runtime Fix

- `http_portal.c` split into four modules: `http_portal.c`, `http_dashboard.c`,
  `http_config_page.c`, `http_compat.c` — each with single responsibility
- AJAX `addOrUpdate()` ID mismatch fixed — td IDs now consistent between static HTML
  and dynamic AJAX updates
- Live wall clock added to dashboard: `Now: H:MM:SS AM/PM | Last poll: H:MM:SS AM/PM`
- `rid=0x21` CyberPower runtime fix — decoded as 16-bit LE seconds (authoritative)
- `rid=0x82` silenced — was incorrectly decoded as runtime; it is the static 300s
  low-battery runtime threshold
- APC Smart-UPS PID=0x0003 added to device DB with correct UID mappings

### v15.12 — USB Hotplug Fix + NUT Variable Parity

**USB Hotplug Fix (hub.c:837 assert):**
- Root cause: ESP-IDF hub.c race condition when USB device disconnected mid-transfer
- Fix: `s_cleanup_pending` volatile flag set in `client_event_cb` BEFORE `s_dev_gone`
- `intr_in_cb` checks both flags before resubmitting
- `usb_lib_task` changed from `portMAX_DELAY` to `pdMS_TO_TICKS(50)` + `vTaskDelay(1)`
- `cleanup_device()` adds `vTaskDelay(pdMS_TO_TICKS(20))` before `usb_host_device_close()`
- Confirmed: 2 clean unplug/replug cycles, no assert, no panic

**NUT Variable Parity (Phase 1):**
- `ups_device_db` extended with 6 NUT static fields per device entry:
  `battery_voltage_nominal_mv`, `battery_runtime_low_s`, `battery_charge_low`,
  `battery_charge_warning`, `input_voltage_nominal_v`, `ups_type`
- All 12 device DB entries populated from NUT DDL + confirmed device testing
- `nut_server.c` now serves: `battery.voltage.nominal`, `battery.runtime.low`,
  `battery.charge.warning`, `input.voltage.nominal`, `ups.type`, `ups.test.result`,
  `ups.delay.shutdown`, `ups.delay.start`, `ups.timer.reboot`, `ups.timer.shutdown`
- All new variables confirmed live in Home Assistant after flash
- Driver version bumped to 15.12

### v15.13 — NUT Variables Lightbox + /status JSON Expansion

- `/status` JSON endpoint expanded with all DB static fields:
  `battery_voltage_nominal_v`, `battery_runtime_low_s`, `battery_charge_low`,
  `battery_charge_warning`, `input_voltage_nominal_v`, `ups_type`, `ups_firmware`,
  `device_mfr`, `device_model`, `device_serial`, `driver_version`
- NUT Variables lightbox added to portal dashboard — click to see full `upsc`-style
  variable list fetched live from `/status`, grouped as battery/input/output/ups/device/driver
- Header shows `upsc ups@<ip>:3493` command; `ups.status` colour-coded green/amber
- `HTTP_PAGE_BUF` increased 8192 → 16384 to accommodate expanded dashboard HTML
- CLAUDE.md workflow updated: build/flash are manual steps, PowerShell hard rule enforced
- README.md restored and updated to v15.13 with full feature and variable tables
