# Session Log — esp32-s3-nut-node

---

## Session 028 — 2026-03-09

### What was accomplished
- v15.8 flashed and confirmed working — boot log clean, GET_REPORT polling every 30s
- APC Back-UPS XS 1500M: input.voltage=120.0V + output.voltage=120.0V live in dashboard and NUT
- Scraped NUT apc-hid.c source — confirmed battery.voltage is expected by NUT but NOT available on APC PID=0002 firmware (all high rids 0x82–0x88 STALL) — not a bug in our driver
- Gap analysis performed — project assessed as feature-complete for current scope
- HA tasks extracted into new standalone project: `ha-automation/`
- Created `docs/HISTORY.md` — full dev narrative v1→v15.8
- Created `docs/COMPATIBLE-UPS.md` — confirmed/expected/likely/incompatible device reference
- Updated `src/current/README.md` — rewritten for v15.8, links all docs
- Created `src/current/git-push.ps1` — reusable PowerShell GitHub push script
- Created `Template_Name/git-push.ps1` — master template for all future projects
- git-push.ps1 is tracked in repo (not gitignored) — edit config block before each push
- TODO.md updated with F10–F13 all marked done

### Files created
- `D:\Users\Stryder\Documents\Claude\Projects\ha-automation\README.md`
- `D:\Users\Stryder\Documents\Claude\Projects\ha-automation\docs\TODO.md`
- `D:\Users\Stryder\Documents\Claude\Projects\ha-automation\docs\INTEGRATIONS.md`
- `D:\Users\Stryder\Documents\Claude\Projects\ha-automation\docs\SESSION_LOG.md`
- `D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\docs\HISTORY.md`
- `D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\docs\COMPATIBLE-UPS.md`
- `D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current\git-push.ps1`
- `D:\Users\Stryder\Documents\Claude\Projects\Template_Name\git-push.ps1`

### Files modified
- `src/current/README.md` — full rewrite for v15.8
- `src/current/main/main.c` — version string v15.7 → v15.8
- `src/current/.gitignore` — git-push.ps1 tracked (not ignored)
- `docs/TODO.md` — F10–F13 added and marked done

### Current firmware state
- **Flashed:** v15.8 ✅
- **NUT variables live:** ups.status, battery.charge, battery.runtime, input.voltage, output.voltage, ups.model, ups.mfr, ups.firmware, device.type
- **battery.voltage:** Not available — APC PID=0002 firmware limitation (confirmed by NUT source)

### Pending (next session)
- Run `git-push.ps1` to push v15.8 to GitHub with tag
- S7 — Set AP password (open AP warning still present on every boot)
- S14 — On-battery test (unplug UPS, verify OB DISCHRG) — deferred to later
- CyberPower GET_REPORT decode (S9–S13) — input/output voltage, battery.runtime from rids 0x23/0x82/0x88
- HA tasks — now tracked in ha-automation project

---

## Session 010 — 2026-03-08

### What was accomplished
- Confirmed v14.25 R9 + R10 firmware written and ready to flash (not yet flashed this session)
- GitHub repo confirmed live at https://github.com/Driftah9/esp32-s3-nut-node
- ha-mcp MCP server installed and connected to Home Assistant
- Full HA health check performed — identified all issues across the system
- Identified 3 dead integrations in HA to remove:
  - ESPHome `esphome-nut01` — early UART-based NUT prototype, abandoned
  - ESPHome `ESPS3-Nut01` — second ESPHome attempt, abandoned
  - NUT integration `10.0.0.6:3493` — pointing at OrangePi (wrong host)
- Confirmed NUT addon in HA is in ERROR state due to stale 10.0.0.6 pointer
- Confirmed UPS sensors all `unavailable` in HA (same cause)
- Investigated Z2M stability — identified TCP keepalive issue with CC2652P2 Ethernet coordinator
- Identified 6 additional HA issues: duplicate Sonoff switches, stale Alexa entities, orphaned media player, stale iBeacon trackers, Remote UI unavailable
- Began work on Anthony arrival automation (GPS + BLE combined trigger)
- Created test automation `Angel is Home - GPS + BLE Test` using S24+ as test device
- Confirmed iBeacon Tracker integration already installed
- Confirmed Bermuda BLE Trilateration installed but limited to 1 proxy (insufficient for trilateration)
- Confirmed Anthony's Pixel 8 Pro has a Bermuda tracker entity

### Problems encountered
- API delete for ESPHome config entries not supported — must be done via HA UI
- GPS zone test couldn't run (phone never left home zone during shutdown test)
- Bermuda distance readings wildly inaccurate (56 ft reported in same room) — root cause: only 1 BLE proxy

### Files changed
- `docs/DECISIONS.md` — added D009 (ESPHome abandoned), D010 (HA NUT to point at ESP32)
- `docs/esp32-s3-nut-node_session_log.md` — created (this file)
- `docs/next_steps.md` — updated (see below)

### Next immediate step
1. Delete 3 dead HA integrations via HA UI (esphome-nut01, ESPS3-Nut01, NUT 10.0.0.6:3493)
2. Add new NUT integration pointing at 10.0.0.190:3493
3. Flash v14.25 R10 to ESP32 (idf.py build flash -p COM3)
