# HID Report Map — esp32-s3-nut-node

**Device:** APC Back-UPS BR1000G
**VID:PID:** 051d:0002
**HID Interface:** 0
**Interrupt IN EP:** 0x81, MPS 8

---

## Confirmed Report ID → NUT variable mappings

| Report ID | Field | Raw → Scaled | NUT Variable | Status |
|---|---|---|---|---|
| 0x0C | byte[?] | direct % | battery.charge | ✅ Confirmed |
| 0x0C | byte[?] | direct s | battery.runtime | ✅ Confirmed |
| 0x13 | bit[?] | 0/1 | input.utility.present | ✅ Confirmed |
| 0x16 | word | direct hex | ups.flags | ✅ Confirmed |
| 0x21 | byte[?] | raw=6, scale=20V → 120.0V | input.voltage | ✅ Confirmed |
| TBD | TBD | TBD | output.voltage | ❌ Not found |
| TBD | TBD | TBD | battery.voltage | ✅ Confirmed (report TBD) |
| TBD | TBD | TBD | ups.load | ✅ Confirmed (report TBD) |

---

## USB String Descriptors (confirmed)

| Descriptor | Value |
|---|---|
| Manufacturer | American Power Conversion |
| Product | Back-UPS BR1000G FW:868.L2 .D USB FW:L2 |
| Serial | 3B1145X11360 |

Read via ESP-IDF USB host cached string API in `ups_usb_hid.c`.

---

## Notes on output.voltage discovery

- Likely lives in same vicinity as input.voltage (Report 0x21 or nearby)
- Strategy: log all unhandled reports during load transition events
- Plausibility gate: 80V–140V after scaling (for 120V grid)
- Do NOT promote until confirmed repeatable across multiple poll cycles

---

## Report descriptor

- Length: 1049 bytes (confirmed for this device)
- Full dump: available in archive from Milestone 2A.1 (REVERT-0001)
