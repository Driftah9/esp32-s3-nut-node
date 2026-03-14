# NUT Node Project — Next Steps (Cold-Start Brief)
# Updated: 2026-03-13 (session 11)

---

## What was completed this session
- ha-mcp MCP server installed and confirmed connected to HA
- Full HA health check — all issues catalogued
- Identified and documented 3 dead HA integrations to remove
- Began Anthony arrival automation — GPS + BLE combined trigger
- Test automation created for S24+ (template for Anthony's version)
- Confirmed iBeacon Tracker installed, Bermuda installed (1 proxy only)
- v14.25 R9 + R10 firmware written — NOT YET FLASHED

## What is confirmed working
- ESP32 NUT node on 10.0.0.190:3493 — stable, serving 17 NUT variables
- v14.25 R10 code written (portal auth + AJAX dashboard + default password warning)
- GitHub repo live: https://github.com/Driftah9/esp32-s3-nut-node
- HA health: Z2M online, all Zigbee sensors good, backups current, all updates applied
- Angel is Home test automation DELETED (automation.angel_is_home_gps_ble_test — removed 2026-03-13)

---

## Exact next steps (priority order)

### 1 — HA UI cleanup (IMMEDIATE — do via browser)
Go to http://10.0.0.10:8123/config/integrations and delete:
- ESPHome `esphome-nut01` (three dots → Delete)
- ESPHome `ESPS3-Nut01` (three dots → Delete)
- Network UPS Tools `10.0.0.6:3493` (three dots → Delete)

Then add new NUT integration:
- Add Integration → Network UPS Tools
- Host: `10.0.0.190` | Port: `3493` | Username: `admin` | Password: `admin`
This will clear the NUT addon ERROR state.

### 2 — Flash v14.25 R10
```powershell
cd D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current
idf.py build flash -p COM3
idf.py monitor -p COM3
```

### 3 — Anthony arrival automation
- Replace `device_tracker.galaxy_s24plus` → Anthony's GPS tracker
- Replace Bermuda tracker → Anthony's Pixel Pro 8 Bermuda tracker
- Message: "Anthony is Home"
- Speaker: as desired

### 5 — HA stale entity cleanup (low priority, cosmetic)
- Delete 3 unavailable iBeacon trackers (l_to_3234_00e6, l_to_3234_00ee, globuse_1_4_9bef) via HA UI
- Delete duplicate Alexa alarm/timer sensors (_2 versions) via HA UI
- Delete orphaned `media_player.master_bedroom_speaker_sendspin` via HA UI

### 6 — Z2M TCP keepalive fix
- Navigate to TubeZB coordinator web UI (find its IP on network)
- Enable TCP keepalive / heartbeat — set to 30-60 seconds
- Monitor Z2M connection state over 48 hours

---

## Blockers / open questions
- ESP32 minor items still deferred: driver.version shows 14.24, AP password not set
- Bermuda only has 1 BLE proxy — distance/trilateration unreliable until more proxies built
- Anthony BLE arrival trigger: currently GPS-only until iBeacon broadcasting confirmed on Pixel 8 Pro
- Linux NUT hub (M9) still on hold — static IP for linuxtest LXC not yet assigned

---

## Project paths
- Source: `D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current\main\`
- GitHub: https://github.com/Driftah9/esp32-s3-nut-node
- ESP32 IP: 10.0.0.190 | NUT: tcp/3493 | Portal: http://10.0.0.190
- Build: ESP-IDF 5.3 PowerShell → `idf.py build flash -p COM3`
