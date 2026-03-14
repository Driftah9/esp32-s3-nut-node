# Revert Guide — esp32-s3-nut-node

---

## REVERT index

| ID | Firmware | Description | Source |
|---|---|---|---|
| REVERT-0001 | v8 (Milestone 2A.1) | USB HID attach + descriptor dump for APC 051d:0002 | archive/v8-milestone-2a1 |
| REVERT-0002 | v13.6 | SoftAP-first portal stability anchor | archive/v13.6-stable |
| REVERT-0003 | v14.7 | Raw-socket portal + STA + NUT plaintext interop | archive/v14.7-stable |
| REVERT-0004 | v14.7-modular | Modules split, no behaviour change | archive/v14.7-modular |
| REVERT-0005 | v14.7-usb-skeleton | ups_state + ups_usb_hid skeleton added | archive/v14.7-usb-skeleton |
| REVERT-0006 | v14.3-stable | **Current stable anchor** — 14 variables confirmed | src/v14.3-stable/ |

---

## How to revert

### Step 1 — Identify the last good anchor
Check which REVERT-ID was last confirmed working. Default to **REVERT-0006** (v14.3-stable) unless you have reason to go further back.

### Step 2 — Restore source files
Copy the full module set from the corresponding `src/` or `archive/` folder into your ESP-IDF project `main/` directory.

### Step 3 — Clean build
```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

### Step 4 — Verify boot sequence
Expected serial output on good boot:
```
[wifi_mgr] SoftAP started — SSID: <configured>
[wifi_mgr] STA connected — IP: 10.0.0.190
[nut_server] Listening on 3493
[ups_usb_hid] Device attached VID:PID 051d:0002
[ups_hid_parser] battery.charge: 100
```

### Step 5 — Verify NUT from Linux
```bash
upsc ups@10.0.0.190
```
Expect at minimum: `battery.charge`, `ups.status`, `input.utility.present`.

---

## Rules for new anchors

When a milestone is confirmed working:
1. Copy the full `main/` source into `archive/<version>-stable/`
2. Add a REVERT-XXXX entry to this file
3. Update `PROJECT.md` REVERT index
4. Update version tags in all module headers

---

## Mini REVERT block format (module headers)

Every module `.c` file must have this block at the top:

```c
/*
 * MODULE: <module_name>
 * RESPONSIBILITY: <one line>
 * REVERT: If this module is broken, replace with the copy from archive/<last-known-good>/
 * LAST KNOWN GOOD: REVERT-0006 (v14.3-stable)
 */
```
