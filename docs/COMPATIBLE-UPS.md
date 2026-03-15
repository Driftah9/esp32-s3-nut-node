# Compatible UPS List — ESP32-S3 NUT Node
**Firmware:** v15.13 | **Updated:** 2026-03-15  
**Full reference:** `docs/usbhid-ups-compat.md`

---

## ✅ Confirmed Working (Tested on this firmware)

| Device | VID:PID | Decode | battery.charge | battery.runtime | battery.voltage | input.voltage | output.voltage | ups.status |
|--------|---------|--------|:-:|:-:|:-:|:-:|:-:|:-:|
| APC Back-UPS XS 1500M | 051D:0002 | interrupt-IN + GET_REPORT | ✅ | ✅ | ❌ N/A¹ | ✅ | ✅ | ✅ |
| APC Back-UPS BR1000G | 051D:0002 | interrupt-IN + GET_REPORT | ✅ | ✅ | ❌ N/A¹ | ✅ | ✅ | ✅ |
| CyberPower ST Series (e.g. CP550HG) | 0764:0501 | interrupt-IN direct | ✅ | ✅ | ✅ | ❌ N/A² | ❌ N/A² | ✅ |

**Notes:**  
¹ APC consumer Back-UPS firmware (PID=0002) does not expose battery voltage via USB HID.
  All high-numbered feature report IDs (0x82–0x88) STALL. This is a firmware limitation,
  not a bug in this driver. NUT's `usbhid-ups` on a PC also does not report battery.voltage
  for these models.  
² CyberPower PID=0501 does not expose input/output voltage via USB HID on consumer AVR
  models. The rid=0x23 data observed is status flags, not voltage. Pending Phase 2
  investigation.

---

## NUT Variables Served (v15.13)

All confirmed devices serve the following NUT variables in addition to live decoded values:

| Variable | Source |
|----------|--------|
| `battery.charge.low` | device DB (per device) |
| `battery.charge.warning` | device DB (per device) |
| `battery.runtime.low` | device DB (per device) |
| `battery.voltage.nominal` | device DB (per device) |
| `input.voltage.nominal` | device DB (per device) |
| `ups.type` | device DB (per device) |
| `ups.test.result` | static: No test initiated |
| `ups.delay.shutdown` | static: 20 |
| `ups.delay.start` | static: 30 |
| `ups.timer.reboot` | static: -1 |
| `ups.timer.shutdown` | static: -1 |
| `battery.type` | static: PbAc |
| `device.type` | static: ups |
| `driver.name` | static: esp32-nut-hid |
| `driver.version` | static: 15.13 |

---

## ⚠️ Expected to Work (Same VID:PID as confirmed devices — untested)

These devices share a VID:PID with a confirmed device. They should work with this
firmware using the same decode path, but have not been physically tested.

### APC Back-UPS family (VID=051D PID=0002)
All use the same USB firmware — same GET_REPORT behavior expected.

| Model | Notes |
|-------|-------|
| Back-UPS XS 1000M | Same PID=0002 |
| Back-UPS ES 850G2 | Same PID=0002 |
| Back-UPS RS series | Same PID=0002 |
| Back-UPS LS series | Same PID=0002 |
| Back-UPS CS series | Same PID=0002 |
| Back-UPS BX****MI | Same PID=0002 |
| Back-UPS BE series | Same PID=0002 |
| Back-UPS BN series | Same PID=0002 |
| BACK-UPS XS LCD | Same PID=0002 |

### CyberPower AVR/ST/CP/SX series (VID=0764 PID=0501)

| Model | Notes |
|-------|-------|
| CP1200AVR | Same PID=0501 |
| CP825AVR-G / LE825G | Same PID=0501 |
| CP1000AVRLCD / CP1500C | Same PID=0501 |
| CP850PFCLCD / CP1000PFCLCD | Same PID=0501 |
| CP1350PFCLCD / CP1500PFCLCD | Same PID=0501 |
| CP1350AVRLCD / CP1500AVRLCD | Same PID=0501 |
| CP900AVR / CPS685AVR / CPS800AVR | Same PID=0501 |
| EC350G / EC750G / EC850LCD | Same PID=0501 |
| BL1250U / AE550 / CPJ500 | Same PID=0501 |

### CyberPower OR/PR/RT/UT rackmount (VID=0764 PID=0601)
Same direct-decode path as PID=0501.

| Model | Notes |
|-------|-------|
| OR2200LCDRM2U / OR700LCDRM1U | Same PID=0601 |
| OR500LCDRM1U / OR1500ERM1U | Same PID=0601 |
| CP1350EPFCLCD / CP1500EPFCLCD | Same PID=0601 |
| PR1500RT2U / PR6000LCDRTXL5U | Same PID=0601 |
| RT650EI / UT2200E | Same PID=0601 |

---

## ❓ Likely Compatible (Standard HID Power Device Class)

These vendors use the standard USB HID Power Device Class. Expected to work via the
generic decode path but have not been physically tested.

| Manufacturer | VID | Models | Decode | Notes |
|-------------|-----|--------|--------|-------|
| APC Smart-UPS | 051D | SMT750I, SMT1500I, SMX series | Standard HID | PID=0003, needs-validation (see issue #1) |
| Eaton / MGE | 0463 | 3S, 5E, 5P, Ellipse, Evolution, Powerware | Standard HID | EU voltage (230V nominal) |
| Tripp Lite | 09AE | SMART series, OMNI series | Standard HID + GET_REPORT | May need GET_REPORT |
| Belkin | 050D | F6C series, Universal UPS | Standard HID | |
| HP | 03F0 | T750/T1000/T1500/T3000 G2/G3 | Standard HID | |
| Dell | 047C | H750E, H950E, H1000E, H1750E | Standard HID | |
| Powercom | 0D9F | Black Knight, Dragon, Smart King | Standard HID | EU voltage (230V nominal) |
| Liebert / Vertiv | 10AF | GXT4, PSI5 | Standard HID | Double-conversion online |

---

## ❌ Not Compatible (Known limitations)

| Device class | Reason |
|-------------|--------|
| USB-to-serial UPS adapters (CDC) | Not a HID device — different protocol |
| RS-232 serial UPS (no USB) | No USB HID interface |
| Network-managed UPS (SNMP only) | No USB interface |
| APC AP9584 serial-USB kit | Serial bridge — different protocol |

---

## Adding Support for a New Device

If your UPS connects but shows no data:

1. Check the boot log for `VID:PID=` and look up in `usbhid-ups-compat.md`
2. If VID:PID is unknown, open `ups_device_db.c` and add an entry with appropriate NUT static fields
3. Check `[XCHK]` warnings in boot log — these identify report IDs not in the parsed
   descriptor (likely vendor-specific reports)
4. Use the GET_REPORT probe sequence (see `ups_get_report.c`) to enumerate feature
   report IDs and find voltage/charge data
5. Add a decoder in `ups_hid_parser.c` for the new device

For detailed HID field reference and Usage ID tables, see `docs/usbhid-ups-compat.md`.
