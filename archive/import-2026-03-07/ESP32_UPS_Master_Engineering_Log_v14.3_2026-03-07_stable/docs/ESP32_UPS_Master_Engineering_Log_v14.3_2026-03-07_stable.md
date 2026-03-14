# ESP32 UPS Master Engineering Log
Version: v14.3
Date: 2026-03-07
Status: stable

## Executive summary

This checkpoint is the v14.3 stable rollback point.

It extends the confirmed v14.2 baseline by validating `input.voltage`
end-to-end through the Linux NUT client.

Confirmed from `upsc ups@10.0.0.190`:

- battery.charge: 100
- battery.runtime: 1400
- input.utility.present: 1
- ups.flags: 0x0000000C
- ups.status: OL
- battery.voltage: 14.000
- input.voltage: 120.0
- ups.load: 12
- device.mfr: American Power Conversion
- device.model: Back-UPS BR1000G FW:868.L2 .D USB FW:L2
- device.serial: 3B1145X11360
- driver.name: esp32-nut-hid
- ups.mfr: American Power Conversion
- ups.model: Back-UPS BR1000G FW:868.L2 .D USB FW:L2

## Confirmed architecture

```text
main.c
 ├── wifi_mgr
 ├── http_portal
 ├── cfg_store
 ├── ups_usb_hid
 │    └── ups_hid_parser
 ├── ups_state
 └── nut_server
```

Data flow:

```text
USB HID reports
   -> ups_usb_hid
   -> ups_hid_parser
   -> ups_state
   -> nut_server
   -> upsc / NUT clients
```

## Confirmed stable variables

Primary telemetry:
- battery.charge
- battery.runtime
- battery.voltage
- input.utility.present
- input.voltage
- ups.flags
- ups.load
- ups.status

Identity / compatibility metadata:
- device.mfr
- device.model
- device.serial
- driver.name
- ups.mfr
- ups.model

## Key parser confirmation

`input.voltage` is now confirmed from HID report `0x21`:

- raw = 6
- selected scale = 20 V
- result = 120.0 V

This was validated both in serial monitor output and through `upsc`.

## Device identity decision

Canonical mapping remains:

- `device.mfr` = real USB manufacturer string
- `device.model` = real USB product string
- `device.serial` = real USB serial string
- `driver.name` = `esp32-nut-hid`

## Hardware confirmed

USB HID UPS observed:
- VID:PID = 051d:0002

Confirmed product string example:
- Back-UPS BR1000G FW:868.L2 .D USB FW:L2

## Files updated in v14.3 stable

- `main/ups_hid_parser.c`
  - promotes `input.voltage` on first plausible observation from report `0x21`
  - keeps `output.voltage` conservative
- `main/ups_usb_hid.c`
  - reads cached USB strings from ESP-IDF host stack
- `main/nut_server.c`
  - exports telemetry + metadata
- `main/ups_state.[ch]`
  - carries telemetry and identity state

## Known-good behavior at this checkpoint

- SoftAP config portal works
- STA obtains LAN IP
- NUT server responds on tcp/3493
- USB attach is detected
- USB strings are read successfully
- HID interrupt reports are parsed continuously
- Linux `upsc` reads both telemetry and metadata
- `input.voltage` is confirmed live

## Remaining work

Not yet confirmed:
- output.voltage

## Recommended next milestone

### v14.4
Target:
- discover and validate `output.voltage`

Best method:
- correlate HID changes during load changes or line/battery transitions
- keep current stable mappings untouched
- only promote `output.voltage` after repeatable plausible evidence

## Rollback index

- v14.0 stable — first modular stable baseline
- v14.1 WIP — telemetry expansion checkpoint
- v14.2 stable — telemetry + USB identity metadata confirmed
- v14.3 stable — input.voltage confirmed

## Notes

This archive should be treated as the new stable source-of-truth rollback package for the v14 branch.
