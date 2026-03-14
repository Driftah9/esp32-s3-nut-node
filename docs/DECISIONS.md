# Decisions Log — esp32-s3-nut-node
<!-- v1.0 | 2026-03-07 | Initial scaffold from PROJECT_UPLOAD -->

Key technical decisions and the reasoning behind them.

---

## D001 — Raw-socket HTTP instead of esp_http_server
**Decision:** Use raw POSIX-style socket HTTP server instead of ESP-IDF's
esp_http_server component.
**Reason:** esp_http_server caused portal save path crashes due to task stack
limits and tight-loop parsing behaviour. Raw socket implementation with manual
header parsing and explicit stack yielding proved stable.
**Confirmed stable:** v14.7

---

## D002 — Larger HTTP task stack
**Decision:** HTTP task stack set larger than ESP-IDF default.
**Reason:** Save path crashes traced to stack exhaustion. Increasing stack size
eliminated crashes.
**Confirmed stable:** v14.7

---

## D003 — SoftAP-first provisioning (no baked-in credentials)
**Decision:** Device always starts SoftAP on first boot. No default STA
SSID/password compiled in.
**Reason:** Device must be deployable to any network without reflashing.
All credentials stored in NVS and entered via portal.

---

## D004 — Modular architecture with mini REVERT blocks
**Decision:** Split monolithic main.c into modules. Each module carries a
REVERT block at the top documenting its last known-good state.
**Reason:** Isolate breakage. USB HID development was destabilising the
confirmed-good networking + NUT baseline. Modules allow USB work to proceed
without risking the stable baseline.
**Implemented:** v14.7 modular baseline

---

## D005 — NUT plaintext (no STARTTLS for MVP)
**Decision:** NUT server runs plaintext on 3493/tcp. STARTTLS not implemented.
**Reason:** LAN-only deployment. All clients are on trusted network.
Complexity of TLS not justified for MVP. Deferred.

---

## D006 — ups_state as shared state model
**Decision:** ups_usb_hid writes decoded values to ups_state struct. Both
nut_server and http_portal read from ups_state. No direct coupling between
USB layer and NUT/HTTP layers.
**Reason:** Clean separation. NUT server does not need to know anything about
USB HID internals. ups_state.valid flag allows safe handling when USB is not
attached.

---

## D007 — Grey cloud (DNS only) for Cloudflare
**Decision:** This project does not use Cloudflare proxying.
**Reason:** N/A — this is a LAN-only device. No external exposure planned.
Note: This decision mirrors obsidian-notes D001 but for different reasons.

---

## D008 — HID report 0x21 scale = 20V for input.voltage (APC only — superseded)
**Decision:** input.voltage decoded from report 0x21, raw value multiplied
by 20 to get volts.
**Confirmed:** raw=6 → 120.0V (matches mains voltage).
**Confirmed:** v14.3
**Superseded by:** D011 (v15.0 usage-based decode)

---

## D009 — ESPHome NUT attempts abandoned
**Decision:** Two early ESPHome-based NUT node prototypes (esphome-nut01,
ESPS3-Nut01) formally abandoned. Boards to be recycled for new projects.
**Reason:** ESPHome UART-based approach could not provide the low-level USB HID
access required. Native ESP-IDF firmware was the correct path.
**Decided:** 2026-03-08 (session 10)

---

## D010 — HA NUT integration to point at ESP32 directly (10.0.0.190)
**Decision:** Home Assistant NUT integration should point at 10.0.0.190:3493
(the ESP32) directly, not via a Linux NUT hub (M9).
**Reason:** ESP32 NUT server is confirmed stable. Linux hub (M9) is deferred.
Pointing HA at the ESP32 directly is the correct current state.
**Decided:** 2026-03-08 (session 10)
**Action required:** Delete old 10.0.0.6:3493 entry via HA UI and re-add pointing at 10.0.0.190.

---

## D011 — Usage-based HID parser (vendor-agnostic, v15.0)
**Decision:** Replace all APC-hardcoded report ID logic with a two-phase
usage-based approach matching NUT's usbhid-ups driver architecture:
1. At USB enumeration, fetch the HID Report Descriptor via USB GET_DESCRIPTOR
   (type 0x22) control transfer.
2. Parse the descriptor into a field map (ups_hid_desc.c) indexed by
   USB HID Power Device usage page/usage ID (0x84/0x85).
3. Decode interrupt-IN reports by looking up fields by usage, not by
   hardcoded report ID.
**Reason:** CyberPower SX550G (VID 0x0764) does not use APC report IDs.
All 338 usbhid-ups supported devices across 29 manufacturers use different
report structures. The only vendor-agnostic approach is usage-based decoding,
which is exactly how the reference NUT driver works.
**New modules:** ups_hid_desc.c / ups_hid_desc.h
**Changed modules:** ups_hid_parser.c/h, ups_usb_hid.c, ups_state.c/h
**Removed:** ups_model_hint_t enum, hardcoded report 0x06/0x0C/0x13/0x14/0x16/0x21 logic
**Implemented:** v15.0 (2026-03-08, session 12)

---

## Template for new decisions
## DXXX — Short title
**Decision:** What was decided.
**Reason:** Why.
**Confirmed stable:** Version or date if applicable.
