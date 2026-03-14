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

## 🔄 Next Milestones

### M6 — UPS Hot-Plug Stability
**Goal:** Plug/unplug UPS without deadlock or firmware crash
**Success criteria:**
- Unplug UPS → ups_state.valid goes false, NUT returns safe defaults
- Replug UPS → re-enumerates cleanly, ups_state.valid returns true
- No watchdog resets during plug/unplug cycle
- Tested 5x plug/unplug without failure
**Relevant code:** ups_usb_hid.c — reattach logic
**Reference:** ups_usb_hid_M15_reattach_patch.c, ups_usb_hid_v14_8_2_reattach_fixed.c

### M7 — Extended HID Report Coverage
**Goal:** Decode additional UPS report IDs for richer telemetry
**Current confirmed report IDs:**
- 0x0C — battery charge + runtime
- 0x13 — AC present
- 0x16 — flags
- 0x21 — input voltage
**Candidates to add:**
- Output voltage (probed in ups_hid_parser_v14_4_output_voltage_probe_dropin.c)
- Temperature (if available on this UPS)
- Battery test status
- Alarm / fault flags
**Success criteria:** Each new variable confirmed via upsc with correct real-world value

### M8 — /status JSON Endpoint Polish
**Goal:** HTTP /status route returns structured JSON useful for dashboards/HA
**Success criteria:**
- Returns ups_state.valid + all confirmed NUT variables
- Includes last_update timestamp
- Returns appropriate values when ups_state.valid = false (not stale data)
- JSON structure documented

### M9 — Home Assistant Integration
**Goal:** HA can poll or receive UPS data without a full NUT server install
**Options to evaluate:**
1. NUT integration in HA (uses upsc under the hood) — may just work already
2. HTTP sensor polling /status JSON endpoint
3. MQTT publish from ESP32 (new module)
**Success criteria:** At least battery.charge, ups.status, input.utility.present
visible as HA entities

### M10 — NVS Config Hardening
**Goal:** Configuration survives power loss and unexpected resets
**Success criteria:**
- All portal-saved settings survive hard power cycle
- Factory reset mechanism (button hold or portal option)
- Config version field to handle future schema changes

### M11 — OTA Firmware Update
**Goal:** Flash new firmware over Wi-Fi without USB cable
**Success criteria:**
- OTA endpoint in HTTP portal or dedicated URL
- Rollback to previous firmware if new firmware fails to boot
- REVERT anchor updated after confirmed OTA

---

## 🗂 Deferred / Future

- **STARTTLS / NUT TLS:** Low priority — plaintext sufficient for LAN-only use
- **Multi-UPS support:** Single UPS per device is fine for current use case
- **Windows Server (10.0.0.22) NUT client:** Test upsc from Windows once M9 done
- **PCB / enclosure:** Hardware packaging once firmware is fully stable
