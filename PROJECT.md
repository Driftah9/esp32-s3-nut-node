# esp32-s3-nut-node
<!-- v1.0 | 2026-03-07 | Initial scaffold from PROJECT_UPLOAD -->

## Summary
ESP32-S3 acts as a USB Host for an APC UPS. Reads telemetry via HID/USB and
exposes it as a NUT upsd-compatible server on 3493/tcp. Standard NUT clients
(upsc, upsmon, etc.) on the LAN query it directly.

## Status
🟢 Active — v14.3 stable baseline confirmed

## Hardware
- Board: ESP32-S3 (USB Host capable)
- UPS: APC Back-UPS BR1000G — VID:PID 051d:0002
- Connection: USB Host (HID interface 0, interrupt IN EP 0x81 MPS 8)

## Network
- SoftAP provisioning: 192.168.4.1
- LAN IP (DHCP): 10.0.0.190 (observed during testing)
- NUT port: 3493/tcp (plaintext)

## Architecture
```
main.c (orchestrator)
 ├── cfg_store.*       — NVS config load/save + defaults
 ├── wifi_mgr.*        — AP+STA start, DHCP, STA IP helper
 ├── http_portal.*     — raw-socket HTTP portal routes
 ├── nut_server.*      — plaintext NUT/upsd-like server
 ├── ups_state.*       — shared UPS state model
 └── ups_usb_hid.*     — USB host + HID UPS decode
       └── ups_hid_parser.*  — HID report decode logic
```

Data flow:
```
USB HID reports → ups_usb_hid → ups_hid_parser → ups_state → nut_server → upsc/NUT clients
                                                            → http_portal (/status JSON)
```

## Confirmed NUT Variables (v14.3)
| Variable              | Example Value                          |
|-----------------------|----------------------------------------|
| battery.charge        | 100                                    |
| battery.runtime       | 1400                                   |
| battery.voltage       | 14.000                                 |
| input.utility.present | 1                                      |
| input.voltage         | 120.0                                  |
| ups.flags             | 0x0000000C                             |
| ups.load              | 12                                     |
| ups.status            | OL                                     |
| device.mfr            | American Power Conversion              |
| device.model          | Back-UPS BR1000G FW:868.L2 .D USB FW:L2 |
| device.serial         | 3B1145X11360                           |
| driver.name           | esp32-nut-hid                          |
| ups.mfr               | American Power Conversion              |
| ups.model             | Back-UPS BR1000G FW:868.L2 .D USB FW:L2 |

## Key Design Rules
- Each module has a "mini REVERT block" at the top describing its responsibility
  and last known-good revert state
- Every change set must: update version tag, update REVERT index, keep module
  mini REVERT blocks current
- If regression: identify last REVERT anchor → rebuild from there → re-apply
  changes one at a time
- Never modify USB modules and networking/NUT modules in the same commit

## Directory Layout
```
esp32-s3-nut-node/
├── PROJECT.md          — this file
├── docs/
│   ├── REVERT-INDEX.md — all known-good anchors
│   ├── MILESTONES.md   — completed + upcoming milestones
│   └── DECISIONS.md    — key technical decisions log
└── src/
    ├── (active baseline source files)
    └── archive/        — superseded drop-in files by version
```
