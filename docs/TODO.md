
---

## Project TODO — esp32-s3-nut-node

Last updated: 2026-03-15 (v15.13)

### Phase 1 — Stability with Current UPS (CyberPower PID=0x0501) [IN PROGRESS]

| # | Task | Status | Notes |
|---|------|--------|-------|
| S1 | CyberPower direct-decode bypass for rids 0x20–0x88 | ✅ Done | v15.3 — all key values decoding |
| S2 | Fix `ups.status` always showing UNKNOWN | ✅ Done | derive_status() was correct; was timing on first render. v15.3 full AJAX |
| S3 | Version strings consistent across main/portal/nut_server | ✅ Done | v15.3 bump |
| S4 | Portal AJAX updates all live cells (runtime, voltages, load) | ✅ Done | v15.3 — /status JSON expanded, AJAX updated |
| S5 | HA integration — delete 3 dead NUT/ESPHome integrations | ✅ Done | Completed |
| S6 | HA integration — add NUT 10.0.0.190:3493 | ✅ Done | HA NUT confirmed working |
| S7 | Set AP password (remove open AP warning) | ✅ Done | Configured |
| S8 | Fix `driver.version` display (was 14.24/14.25, now 15.3) | ✅ Done | v15.3 |
| S9 | Decode rid=0x21 CyberPower runtime | ✅ Done | v15.11 — 16-bit LE seconds, confirmed |
| S10 | rid=0x82 silence — was wrong runtime source | ✅ Done | v15.11 — static 300s threshold, silenced |
| S11 | Decode rid=0x25 (ups.load) | ⏳ Pending Phase 2 | Correct rid not yet identified |
| S12 | Battery scale factor (CPS 1.5× voltage bug check) | ✅ Done | v15.3 — QUIRK_BATT_VOLT_SCALE implemented |
| S13 | Frequency scale factor (CPS freq × 0.1 if > 100) | ✅ Done | v15.3 — QUIRK_FREQ_SCALE_0_1 implemented |
| S14 | Test on-battery behavior — unplug UPS, verify OB DISCHRG | ✅ Done | Confirmed multiple sessions |
| S15 | GitHub release tag v15.12 / v15.13 | ✅ Done | Tagged and pushed |
| S16 | APC GET_REPORT polling — Feature reports for voltages | ✅ Done | v15.8 — ups_get_report.c |
| S17 | USB hotplug fix — hub.c:837 assert | ✅ Done | v15.12 — s_cleanup_pending flag |
| S18 | NUT variable parity Phase 1 | ✅ Done | v15.12 — all static vars confirmed in HA |
| S19 | NUT Variables lightbox in portal | ✅ Done | v15.13 — confirmed working |

### Phase 2 — Zigbee Coprocessor (ESP32-H2 Mini)

**Goal:** Add an ESP32-H2 Mini as an optional Zigbee coprocessor. Users who add the H2
hardware can enable Zigbee in the config portal — the node then broadcasts UPS state over
Zigbee in addition to (or instead of) WiFi NUT. Users without the H2 are unaffected.
The ESP32-S3 handles USB HID and NUT; the H2 handles Zigbee transport.
Communication between S3 and H2 via UART.

**Prerequisites:** Phase 1 stable + HA NUT integration working.

**Design principle:** Zigbee is opt-in. The H2 coprocessor is an add-on — the base
firmware runs identically with or without it. Enable flag stored in NVS; if H2 not
present or not enabled, all Zigbee code paths are dormant.

| # | Task | Status | Notes |
|---|------|--------|-------|
| Z1 | Research ESP32-H2 Mini Zigbee coprocessor examples (esp-zigbee-sdk) | ⏳ Pending | |
| Z2 | Define S3↔H2 UART protocol (UPS state update messages) | ⏳ Pending | Simple JSON or binary struct |
| Z3 | Implement H2 firmware: Zigbee NUT device profile | ⏳ Pending | Custom ZCL cluster for UPS data |
| Z4 | Implement S3 UART TX task: broadcast UPS state every N seconds | ⏳ Pending | Only runs when Zigbee enabled in NVS |
| Z5 | Wire H2 Mini to S3: UART TX/RX + power (3.3V from S3 or shared rail) | ⏳ Pending | |
| Z6 | HA Zigbee2MQTT: add H2 as Zigbee device, map UPS entities | ⏳ Pending | |
| Z7 | Add Zigbee enable toggle to config portal | ⏳ Pending | NVS flag — disabled by default, no H2 required to run firmware |
| Z8 | H2 presence detection on boot via UART ping | ⏳ Pending | Auto-disable Zigbee if H2 not responding |
| Z9 | Fallback: if WiFi STA disconnects for > 60s, activate Zigbee TX | ⏳ Pending | Resilience feature — only when H2 present |
| Z10 | Document H2 wiring + flashing in docs/zigbee-coprocessor.md | ⏳ Pending | For users who want to add the H2 optionally |

**Hardware notes:**
- ESP32-H2 Mini: IEEE 802.15.4 (Zigbee/Thread), no WiFi, no BLE 5 classic
- Connects to S3 via UART2 (GPIO17/18 on S3 devkit)
- H2 must be flashed separately (USB-C on H2 board)
- H2 RCP (Radio Co-Processor) mode: H2 acts as Zigbee radio for S3 via UART
  OR standalone mode: H2 runs its own Zigbee stack, S3 just sends UPS data over UART
- Base firmware ships with Zigbee disabled — no behavior change for users without H2

### Phase 3 — Future / Backlog

| # | Task | Notes |
|---|------|-------|
| F1 | Multi-UPS support (second OTG via USB hub) | Low priority |
| F2 | NUT INSTCMD (test.battery.start, shutdown) | Requires USB control transfers |
| F3 | HTTPS portal (mbedTLS) | Currently plaintext only |
| F4 | OTA firmware update via portal | Flash write from HTTP POST |
| F5 | Prometheus /metrics endpoint | For Grafana scraping without NUT |
| F6 | Support Tripp Lite (feature report GET_REPORT) | Different device class |
| F7 | Support APC vendor pages 0xFF84/0xFF85 | Already noted, needs test device |
| F8 | APC GET_REPORT Feature polling for voltages | ✅ Done v15.8 — promoted to S16 |
| F9 | Add static NUT diagnostic vars: `battery.type`, `battery.charge.low`, `device.type` | ✅ Done v15.8 — device.type added to nut_server.c |
| F10 | git-push.ps1 template added to Template_Name/templates/ | ✅ Done 2026-03-09 |
| F11 | HA tasks extracted to ha-automation project | ✅ Done 2026-03-09 |
| F12 | docs/HISTORY.md — full dev history v1→v15.8 | ✅ Done 2026-03-09 |
| F13 | docs/COMPATIBLE-UPS.md — confirmed/expected/likely device list | ✅ Done 2026-03-09 |

