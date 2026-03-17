# Confirmed Compatible UPS Devices — ESP32-S3 NUT Node

Devices in this list have been physically tested and verified working with this firmware.  
Each entry was submitted via a [UPS Compatibility Report](https://github.com/Driftah9/esp32-s3-nut-node/issues/new?template=ups-compatibility-report.yml) and confirmed by the maintainer.

**Have a working device not listed?** → [Submit a compatibility report](https://github.com/Driftah9/esp32-s3-nut-node/issues/new?template=ups-compatibility-report.yml)

---

## ✅ Confirmed Working

| Device | VID:PID | Firmware | Status | Submitted by | Issue | Date |
|--------|---------|----------|--------|--------------|-------|------|
| APC Back-UPS XS 1500M | 051D:0002 | v15.13 | ✅ Confirmed | @Driftah9 | — | 2026-03-09 |
| APC Back-UPS BR1000G | 051D:0002 | v15.13 | ✅ Confirmed | @Driftah9 | — | 2026-03-09 |
| CyberPower CP550HG / SX550G | 0764:0501 | v15.13 | ✅ Confirmed | @Driftah9 | — | 2026-03-09 |
| APC Smart-UPS C 1500 | 051D:0003 | v15.15 | ✅ Confirmed | @omarkhali | #1 | 2026-03-17 |

---

## ⚠️ Expected to Work (Same VID:PID — Untested)

These share a VID:PID with a confirmed device. The same decode path applies.  
If you test one, please [submit a report](https://github.com/Driftah9/esp32-s3-nut-node/issues/new?template=ups-compatibility-report.yml)!

### APC Back-UPS family (VID=051D PID=0002)

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

### CyberPower AVR series (VID=0764 PID=0501)

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

---

## ❓ Likely Compatible (Standard HID Power Device Class)

These use the standard USB HID Power Device class. Expected to work via generic decode path.

| Manufacturer | VID | Models | Notes |
|-------------|-----|--------|-------|
| APC Smart-UPS | 051D | SMT750I, SMT1500I, SMX series | PID=0003, same decode path as confirmed Smart-UPS C 1500 |
| Eaton / MGE | 0463 | 3S, 5E, 5P, Ellipse, Evolution, Powerware | Standard HID |
| Tripp Lite | 09AE | SMART series, OMNI series | May need GET_REPORT |
| Belkin | 050D | F6C series, Universal UPS | Standard HID |
| HP | 03F0 | T750/T1000/T1500/T3000 G2/G3 | Standard HID |
| Dell | 047C | H750E, H950E, H1000E, H1750E | Standard HID |
| Powercom | 0D9F | Black Knight, Dragon, Smart King | Standard HID |
| CyberPower OR/PR | 0764 | PID=0601 rackmount series | Same direct decode path as 0501 |
| Liebert / Vertiv | 10AF | GXT4, PSI5 | Standard HID |

---

## ❌ Not Compatible

| Device class | Reason |
|-------------|--------|
| USB-to-serial adapters (CDC) | Not a HID device — different protocol |
| RS-232 serial UPS (no USB) | No USB interface |
| Network UPS (SNMP only) | No USB interface |
| APC AP9584 serial-USB kit | Serial bridge, not HID |

---

## Metrics Reference

| Field | Description | Unit | Notes |
|-------|-------------|------|-------|
| `ups.status` | OL / OB / CHRG / DISCHRG / LB | string | Live decoded |
| `battery.charge` | Battery charge | % | Live decoded |
| `battery.charge.low` | Low battery threshold | % | From device DB |
| `battery.charge.warning` | Warning threshold | % | From device DB |
| `battery.runtime` | Estimated runtime remaining | seconds | Live decoded |
| `battery.runtime.low` | Low runtime threshold | seconds | From device DB |
| `battery.voltage` | Battery voltage | V | Live, not all models |
| `battery.voltage.nominal` | Nominal battery voltage | V | From device DB |
| `input.voltage` | AC input voltage | V | GET_REPORT, APC only |
| `input.voltage.nominal` | Nominal input voltage | V | From device DB |
| `output.voltage` | AC output voltage | V | GET_REPORT, APC only |
| `ups.type` | UPS topology | string | From device DB |
| `ups.load` | Load percentage | % | Pending Phase 2 |

---

*This file is automatically updated when a compatibility report issue is labeled `confirmed`.*  
*Last manual seed: 2026-03-17 — v15.15*
