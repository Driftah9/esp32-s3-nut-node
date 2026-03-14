# ESP32 UPS Master Engineering Log
Version: v14.2
Date: 2026-03-07
Status: WIP (metadata variables)

## Summary
This minor version builds on the working v14.1 telemetry checkpoint, where the node already exposes live UPS values for battery charge, runtime, voltage candidate, load candidate, utility presence, flags, and status.

## Newly added in v14.2
- `device.mfr`
- `device.model`
- `driver.name`

Compatibility aliases also exposed:
- `ups.mfr`
- `ups.model`

## Notes
- `device.mfr` / `device.model` are the preferred modern NUT-style metadata fields.
- `ups.mfr` / `ups.model` remain useful as compatibility aliases.
- `driver.name` is now exposed as `esp32-usbhid-apc`.
- Until USB string descriptor reads are implemented, metadata is best-effort:
  - Vendor 0x051d -> `APC`
  - Report descriptor length 1049 -> `APC HID 1049-byte profile`
  - Report descriptor length 1133 -> `APC HID 1133-byte profile`

## Already working live variables
- `battery.charge`
- `battery.runtime`
- `battery.voltage`
- `ups.load`
- `input.utility.present`
- `ups.flags`
- `ups.status`

## Still pending
- `input.voltage`
- `output.voltage`

## Next recommended step
Implement USB string descriptor reads so `device.model` and `device.mfr` can report actual product strings instead of best-effort profile names.
