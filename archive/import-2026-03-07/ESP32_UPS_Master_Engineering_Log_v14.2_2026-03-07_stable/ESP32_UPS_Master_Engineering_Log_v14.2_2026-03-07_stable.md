# ESP32 UPS Master Engineering Log
Version: v14.2
Date: 2026-03-07
Status: stable

## Executive summary

This checkpoint is the new stable rollback point after validating both live UPS telemetry and USB identity metadata through the Linux NUT client.

Confirmed from `upsc ups@10.0.0.190`:

- battery.charge: 100
- battery.runtime: 1400
- input.utility.present: 1
- ups.flags: 0x0000000C
- ups.status: OL
- battery.voltage: 14.000
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

## Device identity decision

For compatibility, the canonical mapping is now locked in as:

- `device.mfr` = real USB manufacturer string
- `device.model` = real USB product string
- `device.serial` = real USB serial string
- `driver.name` = `esp32-nut-hid`

This is preferred over guessed values or descriptor-length-derived names.

## Hardware confirmed

USB HID UPS observed:
- VID:PID = 051d:0002

Confirmed product string example:
- Back-UPS BR1000G FW:868.L2 .D USB FW:L2

## Files updated in v14.2 stable

- `main/ups_usb_hid.c`
  - reads cached USB string descriptors from ESP-IDF host stack
  - publishes manufacturer/product/serial into shared state
- `main/nut_server.c`
  - exports `device.*`, `driver.name`, and compatibility aliases
- `main/ups_state.[ch]`
  - carries identity metadata
- `main/ups_hid_parser.[ch]`
  - continues validated candidate support for battery voltage and UPS load

## Known-good behavior at this checkpoint

- SoftAP config portal works
- STA obtains LAN IP
- NUT server responds on tcp/3493
- USB attach is detected
- USB strings are read successfully
- HID interrupt reports are parsed continuously
- Linux `upsc` reads both telemetry and identity metadata

## Remaining work

Not yet exposed:
- input.voltage
- output.voltage

These remain the next parser milestone.

## Recommended next milestone

### v14.3
Target:
- map `input.voltage`
- map `output.voltage`

Method:
- continue HID report correlation
- optionally move toward descriptor-driven usage mapping if needed

## Rollback index

- v14.0 stable — first modular stable baseline
- v14.1 WIP — telemetry expansion checkpoint
- v14.2 stable — telemetry + USB identity metadata confirmed

## Notes

This archive should be treated as the new stable source-of-truth rollback package for the v14 branch.
