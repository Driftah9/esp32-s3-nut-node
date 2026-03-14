# REVERT Index — esp32-s3-nut-node
<!-- v1.0 | 2026-03-07 | Imported from source-of-truth log -->

All confirmed known-good anchors. When a regression occurs, identify the
last anchor that boots reliably and rebuild from there.

---

## REVERT-0001 — Firmware v8 (Milestone 2A.1)
**Status:** ✅ Confirmed good
**What works:** USB HID attach + descriptor dump for APC 051d:0002
**Notes:** USB baseline anchor only. No NUT, no portal.

---

## REVERT-0002 — Firmware v13.6 "Recovery baseline"
**Status:** ✅ Confirmed good
**What works:** SoftAP-first portal stability (esp_http_server era)
**Notes:** Portal stable, STA join works. NUT not yet present.

---

## REVERT-0003 — Firmware v14.7 "LAN NUT interop baseline"
**Status:** ✅ CONFIRMED (end-to-end tested)
**What works:**
- Raw-socket HTTP portal
- STA join to LAN (DHCP)
- NUT plaintext server interop confirmed with Linux upsc
**Notes:** Stability came from: larger HTTP task stack, parser yielding during
header reads, stable AP+STA provisioning flow.

---

## REVERT-0004 — Firmware v14.7 "modular baseline"
**Status:** ✅ Confirmed good
**What works:** v14.7 split into modules (no intended behaviour change)
**Notes:** Safe split point — all v14.7 functionality preserved.

---

## REVERT-0005 — Firmware v14.7 "modular + USB skeleton"
**Status:** ✅ Confirmed good
**What works:** ups_state + ups_usb_hid skeleton added. NUT reads ups_state
(demo values). USB decode not yet wired.
**Notes:** Baseline before real HID decode implementation.

---

## REVERT-0006 — Firmware v14.3 "HID decode + extended variables"
**Status:** ✅ CONFIRMED (end-to-end tested, 2026-03-07)
**What works:** Full HID decode pipeline. 14 NUT variables confirmed live
including battery.voltage, input.voltage, ups.load, device identity strings.
**Confirmed from:** upsc ups@10.0.0.190
**Notes:** input.voltage confirmed from HID report 0x21 (raw=6, scale=20V → 120.0V)
**Source files:** ESP32_UPS_Master_Engineering_Log_v14.3_2026-03-07_stable package
**Limitation:** APC-only. Hardcoded report IDs 0x06/0x0C/0x13/0x14/0x16/0x21.

---

## REVERT-0007 — Firmware v14.18 (NUT recv timeout + no-banner)
**Status:** ✅ CONFIRMED
**Notes:** Fixed NUT STARTTLS issue. No connection-time banner.

---

## REVERT-0008 — Firmware v14.24 (XS1500M model-aware decode)
**Status:** ✅ CONFIRMED
**Notes:** Model hint for BR1000G / XS1500M. APC-only.

---

## REVERT-0009 — Firmware v14.25 R10 (Portal auth + AJAX + default password warning)
**Status:** ✅ CONFIRMED (written, not yet flashed as of session 11)
**Notes:** Portal authentication, AJAX polling, default password banner.

---

## REVERT-0010 — Firmware v15.0 "Usage-based HID parser"
**Status:** 🔶 WRITTEN — pending build + flash
**What's new:**
- ups_hid_desc.c/h — HID Report Descriptor parser (new module)
- ups_hid_parser.c — completely rewritten, usage-based decode
- ups_usb_hid.c — adds GET_DESCRIPTOR control transfer to fetch report desc
- ups_state.c/h — removed ups_model_hint_t (no longer needed)
- CMakeLists.txt — adds ups_hid_desc.c
**Expected result:** Works with CyberPower SX550G and all 338 usbhid-ups devices
**To flash:**
```powershell
cd D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current
idf.py build flash -p COM3
```
**Added:** 2026-03-08 session 12

---

## REVERT-0017 — Firmware v15.7 (Dynamic dashboard + sdkconfig.defaults)
**Status:** ✅ CONFIRMED
**Date:** 2026-03-09
**What works:** Dynamic compat page, sdkconfig.defaults locked for 16MB flash
**Notes:** Last stable before GET_REPORT rewrite

---

## REVERT-0018 — Firmware v15.8 (GET_REPORT voltage polling — PRODUCTION)
**Status:** ✅ CONFIRMED FLASHED 2026-03-09
**What works:**
- GET_REPORT single-owner queue pattern (portMAX_DELAY fix)
- APC rid=0x17 = 120V AC line voltage, polls every 30s
- input.voltage + output.voltage live in dashboard + NUT
- 3 confirmed UPS devices: APC XS1500M, APC BR1000G, CyberPower ST Series
- docs/HISTORY.md, docs/COMPATIBLE-UPS.md, README rewritten
- git-push.ps1 in repo root
**NUT variables:** ups.status, battery.charge, battery.runtime, input.voltage, output.voltage, ups.model, ups.mfr, ups.firmware, device.type
**Known missing:** battery.voltage (APC PID=0002 firmware limitation — confirmed not available)

---

## Adding new anchors
When a new milestone is confirmed end-to-end:
1. Add entry here with REVERT-XXXX (next number)
2. Tag status as ✅ CONFIRMED with date
3. List exactly what was tested
4. Reference source files / archive location
