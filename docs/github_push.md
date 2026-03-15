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
- ups_usb_hid.c v15.12: graceful USB hot-unplug — fixes hub.c:837 assert
  - s_cleanup_pending flag blocks intr_in_cb resubmit on DEV_GONE
  - usb_lib_task uses 50ms timeout + vTaskDelay(1) yield for cleanup ordering
  - cleanup_device() calls usb_host_device_close() after vTaskDelay(20) guard
