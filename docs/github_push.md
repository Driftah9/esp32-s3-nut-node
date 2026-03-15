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
v15.12

## Commit Message
- http_portal.c refactored: split into http_portal.c, http_dashboard.c, http_config_page.c
- AJAX id mismatch fixed: addOrUpdate id now matches static HTML td ids (td_charge etc)
- Wall clock added to dashboard: shows Now/Last poll in H:MM:SS AM/PM format
- rid=0x21 CyberPower runtime fixed: 16-bit LE seconds, authoritative source
- rid=0x82 silenced: static 300s threshold, was incorrectly decoded as runtime
- APC Smart-UPS PID 0x0003 added to device DB with non-standard UID mappings
- ups_usb_hid.c: graceful USB hot-unplug fix for hub.c:837 assert on DEV_GONE
- NUT variable parity: added battery.voltage.nominal, battery.runtime.low, battery.charge.warning, input.voltage.nominal, ups.type, ups.test.result, ups.delay.shutdown, ups.timer.reboot per device from DB
- ups_device_db: NUT static fields added to all 12 device entries (confirmed + expected)
- nut_server: driver version 15.12, all new NUT variables served and confirmed in HA
