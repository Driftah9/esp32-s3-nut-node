# Milestones — esp32-s3-nut-node
<!-- v1.0 | 2026-03-07 | Initial scaffold from PROJECT_UPLOAD -->

---

## ✅ Completed Milestones

### M1 — SoftAP Provisioning Portal
**Confirmed:** v13.6
- SoftAP appears on first boot at 192.168.4.1
- Routes: /config, /save, /status, /reboot (raw-socket HTTP)
- No baked-in STA credentials on first boot
- Save does not crash; reboot transitions to STA

### M2 — STA Join + LAN Reachability
**Confirmed:** v14.7
- DHCP IP obtained on LAN (10.0.0.190 observed)
- Portal accessible at STA IP after provisioning
- AP+STA coexistence stable

### M2A.1 — USB HID Baseline (separate milestone)
**Confirmed:** v8
- USB Host mode operational
- APC UPS VID:PID 051d:0002 enumerated
- HID interface 0 claimed
- Interrupt IN EP 0x81 MPS 8 opened
- HID Report Descriptor dumped successfully

### M3 — NUT Plaintext Server Interop
**Confirmed:** v14.7
- upsd-compatible server on 3493/tcp
- upsc ups@<esp-ip> returns variables from LAN Linux host
- No STARTTLS required (plaintext MVP)

### M4 — Modular Architecture
**Confirmed:** v14.7 modular baseline
- main.c reduced to orchestrator only
- Modules: cfg_store, wifi_mgr, http_portal, nut_server, ups_state, ups_usb_hid
- Mini REVERT blocks in each module

### M5 — Real HID Decode Pipeline
**Confirmed:** v14.3 (2026-03-07) ← CURRENT STABLE BASELINE
- USB HID reports decoded end-to-end
- ups_state.valid = true from real hardware data
- 14 NUT variables confirmed live via upsc
- battery.voltage, input.voltage, ups.load confirmed
- Device identity strings (mfr, model, serial) confirmed

---

### M6 — Multi-Vendor USB HID Support
**Confirmed:** v15.3 (2026-03-09)
- Device database (ups_device_db.c) with VID:PID + quirk flags
- CyberPower ST Series (0764:0501) fully decoding via interrupt-IN
- APC Back-UPS (051D:0002) decoding via interrupt-IN + GET_REPORT
- 3 devices confirmed: APC XS1500M, APC BR1000G, CyberPower ST Series

### M7 — GET_REPORT Feature Report Polling
**Confirmed:** v15.8 (2026-03-09)
- Single-owner queue pattern — only usb_client_task issues control transfers
- Root cause found: portMAX_DELAY was blocking the queue service loop
- APC rid=0x17 = 120V AC line voltage confirmed, polls every 30s
- input.voltage + output.voltage live in dashboard, NUT, and HA

### M8 — Production Documentation + GitHub Release
**Confirmed:** v15.8 (2026-03-09)
- docs/HISTORY.md — full dev narrative v1→v15.8
- docs/COMPATIBLE-UPS.md — confirmed/expected/likely/incompatible reference
- README.md rewritten for v15.8 state
- git-push.ps1 reusable push script in repo + Template_Name
- ha-automation/ project created for all HA-side work

---

### M9 — USB Hotplug Stability
**Confirmed:** v15.12 (2026-03-15)
- hub.c:837 assert fixed via `s_cleanup_pending` flag + `usb_lib_task` 50ms timeout
- 2 confirmed clean unplug/replug cycles, no panic, no reboot
- NUT server polling continued uninterrupted during hotplug test

### M10 — Full NUT Variable Parity (Phase 1)
**Confirmed:** v15.12 (2026-03-15)
- All static NUT variables now served per device from DB
- `battery.voltage.nominal`, `battery.runtime.low`, `battery.charge.warning`,
  `input.voltage.nominal`, `ups.type`, `ups.test.result`, `ups.delay.shutdown`,
  `ups.timer.reboot` all confirmed live in Home Assistant

### M11 — NUT Variables Portal Lightbox
**Confirmed:** v15.13 (2026-03-15)
- Full `upsc`-style variable list accessible from portal dashboard
- `/status` JSON expanded with all static DB fields
- Confirmed working in browser: all groups render, live data, `×` closes

---

## 🔄 Next Milestones

### M12 — NUT Variable Parity Phase 2 (Live Variables)
**Goal:** Decode `ups.load`, `input.voltage` live, `output.frequency` from GET_REPORT
**Success criteria:**
- `ups.load` decoded for CyberPower (correct rid identified and wired)
- `input.voltage` live (not just nominal) for CyberPower
- `output.frequency` decoded and served

### M13 — Zigbee Coprocessor (ESP32-H2)
**Goal:** Optional ESP32-H2 add-on for Zigbee UPS state broadcast
**Prerequisites:** ESP32-H2 hardware obtained by Stryder
**Design:** S3 handles USB HID + NUT; H2 handles Zigbee transport via UART
- Zigbee is opt-in — base firmware unchanged without H2
- H2 presence detected on boot via UART ping

### M14 — USB Hub Multi-UPS Support
**Goal:** Support multiple UPS devices via USB hub on the ESP32-S3 OTG port
**Prerequisites:** Research max devices ESP-IDF USB Host can handle simultaneously
**Questions to answer:**
- Max simultaneous HID devices supported by ESP-IDF USB Host
- Memory/IRAM impact per additional device
- NUT naming convention for multiple UPS (ups1, ups2, etc.)

---

## 🗂 Deferred / Future

- **STARTTLS / NUT TLS:** Low priority — plaintext sufficient for LAN-only use
- **PCB / enclosure:** Hardware packaging once firmware fully stable
- **OTA firmware update:** Flash over WiFi without USB cable
- **NUT INSTCMD:** `test.battery.start`, shutdown commands via USB control transfer
