# GitHub Push — esp32-s3-nut-node

> Claude updates this file whenever code changes during a session.
> The git-push.ps1 script reads this file for all push details.
> Keep this current — it is the single source of truth for every push.

---

## Project
esp32-s3-nut-node

## Repo
https://github.com/Driftah9/esp32-s3-nut-node

## Visibility
public

## Branch
main

## Version
v15.13

## Commit Message
- NUT Variables lightbox added to portal dashboard: click to see full upsc-style variable list
- Lightbox fetches /status live, grouped as battery/input/output/ups/device/driver
- Shows upsc ups@<ip>:3493 command in header, ups.status coloured green/amber
- /status JSON expanded: now includes battery_voltage_nominal_v, battery_runtime_low_s,
  battery_charge_low, battery_charge_warning, input_voltage_nominal_v, ups_type,
  ups_firmware, device_mfr, device_model, device_serial, driver_version
- HTTP_PAGE_BUF increased 8192 -> 16384 to accommodate expanded dashboard
- http_portal.c: ups_device_db lookup on /status for per-device static fields
- Dashboard and driver row version strings bumped to v15.13
- CLAUDE.md updated: build/flash are manual steps, CLI must not run idf.py
