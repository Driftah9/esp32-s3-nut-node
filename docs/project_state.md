# Project State — esp32-s3-nut-node

## Last Updated
Date: 2026-03-15 (Session 17 — Desktop)
Updated by: Claude Desktop

---

## Current Status

| Field | Value |
|-------|-------|
| Project Version | v15.13 |
| Overall Status | 🟢 Verified — flashed, lightbox confirmed working, pushed |
| Last Action | NUT Variables lightbox + /status JSON expansion |
| Last Action Result | PASS — lightbox opens, all groups render correctly, pushed v15.13 |

---

## Last Manual Run (Session 17)

### What was done
- `ups_device_db.h/c` — 6 NUT static fields added to all 12 device entries
- `nut_server.c` — now serves battery.voltage.nominal, battery.runtime.low,
  battery.charge.warning, input.voltage.nominal, ups.type (per DB), plus static:
  ups.test.result, ups.delay.shutdown, ups.delay.start, ups.timer.reboot, ups.timer.shutdown
- DRIVER_VERSION bumped to 15.12
- `.gitignore` updated — docs/build.log, docs/monitor.log excluded from tracking
- build.log removed from GitHub via `git rm --cached`
- GitHub push completed — v15.12 tag pending

### HA Validation Result
- `sensor.ups_nominal_battery_voltage` = 12.0 V ✅
- `sensor.ups_nominal_input_voltage` = 120 V ✅
- `sensor.ups_ups_type` = line-interactive ✅
- `sensor.ups_ups_shutdown_delay` = 20 ✅
- `sensor.ups_battery_charge` = 94% ✅
- `sensor.ups_status` = Online ✅

---

## Current Known Issues

| Issue | Severity | Status |
|-------|----------|--------|
| IRAM nearly full (99.99%) | Medium | Monitor — any new IRAM_ATTR function will fail to link |
| App version string dirty tag | Low | Cosmetic — run git tag v15.12 |
| ups.load MISSING for CyberPower | Low | Pending — correct rid not yet identified |

---

## Next Recommended Step

1. `git tag v15.12 && git push origin v15.12` — clean version string
2. Web portal NUT lightbox — show all NUT vars via popup (designed, not yet built)
3. GitHub issue #1 comment — point APC Smart-UPS C reporter to v15.12
4. Phase 2 NUT vars — ups.load, input.voltage live, output.frequency (needs GET_REPORT)
