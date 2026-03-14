# Compatible UPS List — ESP32-S3 NUT Node
**Firmware:** v15.8 | **Updated:** 2026-03-09  
**Full reference:** `docs/usbhid-ups-compat.md`

---

## ✅ Confirmed Working (Tested on this firmware)

| Device | VID:PID | Decode | battery.charge | battery.runtime | battery.voltage | input.voltage | output.voltage | ups.status |
|--------|---------|--------|:-:|:-:|:-:|:-:|:-:|:-:|
| APC Back-UPS XS 1500M | 051D:0002 | interrupt-IN + GET_REPORT | ✅ | ✅ | ❌ N/A¹ | ✅ | ✅ | ✅ |
| APC Back-UPS BR1000G | 051D:0002 | interrupt-IN + GET_REPORT | ✅ | ✅ | ❌ N/A¹ | ✅ | ✅ | ✅ |
| CyberPower ST Series (e.g. CP550HG) | 0764:0501 | interrupt-IN direct | ✅ | ✅ | ✅ | ⚠️² | ⚠️² | ✅ |

**Notes:**  
¹ APC consumer Back-UPS firmware (PID=0002) does not expose battery voltage via USB HID.
  All high-numbered feature report IDs (0x82–0x88) STALL. This is a firmware limitation,
  not a bug in this driver. NUT's `usbhid-ups` on a PC also does not report battery.voltage
  for these models.  
² CyberPower input/output voltage is available via GET_REPORT rids 0x23/0x82 but not yet
  wired into the decoder. Pending task S9–S13.

---

## ⚠️ Expected to Work (Same VID:PID as confirmed devices — untested)

These devices share a VID:PID with a confirmed device. They should work with this
firmware using the same decode path, but have not been physically tested.

### APC Back-UPS family (VID=051D PID=0002)
All use the same USB firmware — same GET_REPORT behavior expected.

| Model | Notes |
|-------|-------|
| Back-UPS XS 1000M | Same PID=0002 |
| Back-UPS ES series | Same PID=0002 |
| Back-UPS RS series | Same PID=0002 |
| Back-UPS LS series | Same PID=0002 |
| Back-UPS CS series | Same PID=0002 |
| Back-UPS BX****MI | Same PID=0002 |
| BACK-UPS XS LCD | Same PID=0002 |

### CyberPower (VID=0764 PID=0501)
| Model | Notes |
|-------|-------|
| CP1200AVR | Same PID=0501 |
| CP825AVR-G / LE825G | Same PID=0501 |
| CP1000AVRLCD | Same PID=0501 |
| CP1500C | Same PID=0501 |
| CP850PFCLCD | Same PID=0501 |
| CP1000PFCLCD | Same PID=0501 |
| CP1350AVRLCD | Same PID=0501 |
| CP1500AVRLCD | Same PID=0501 |
| CP900AVR | Same PID=0501 |
| EC350G / EC750G | Same PID=0501 |

---

## ❓ Likely Compatible (Standard HID Power Device Class)

These vendors use the standard USB HID Power Device Class. They should work with the
generic decode path but have not been tested. No quirks expected.

| Manufacturer | VID | Models | Notes |
|-------------|-----|--------|-------|
| APC Smart-UPS | 051D | SMT750I, SMT1500I, SMX series | Different PID from Back-UPS |
| Eaton / MGE | 0463 | 3S, 5E, 5P, Ellipse, Evolution | Standard HID |
| Tripp Lite | 09AE | SMART series, OMNI series | May need GET_REPORT |
| Belkin | 050D | F6C series | Standard HID |
| HP | 03F0 | T750/T1000/T1500/T3000 G2/G3 | Standard HID |
| Dell | 047C | H750E, H950E, H1000E, H1750E | Standard HID |
| Powercom | 0D9F | Black Knight, Dragon, Smart King | Standard HID |
| CyberPower | 0764 | PID=0601 (OR/PR series) | Different PID, may need tuning |
| Liebert | 10AF | GXT4, PSI5 | Standard HID |

---

## ❌ Not Compatible (Known limitations)

| Device class | Reason |
|-------------|--------|
| USB-to-serial UPS adapters | Present as CDC serial device, not HID |
| RS-232 serial UPS (no USB) | No USB HID interface |
| Network-managed UPS (SNMP only) | No USB interface |
| APC AP9584 serial-USB kit | Serial bridge — different protocol |

---

## Adding Support for a New Device

If your UPS connects but shows no data:

1. Check the boot log for `VID:PID=` and look up in `usbhid-ups-compat.md`
2. If VID:PID is unknown, open `ups_device_db.c` and add an entry
3. Check `[XCHK]` warnings in boot log — these identify report IDs not in the parsed
   descriptor (likely vendor-specific reports)
4. Use the GET_REPORT probe sequence (see `ups_get_report.c`) to enumerate feature
   report IDs and find voltage/charge data
5. Add a decoder in `ups_hid_parser.c` for the new device

For detailed HID field reference and Usage ID tables, see `docs/usbhid-ups-compat.md`.
