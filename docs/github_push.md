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
v15.10

## Commit Message
- CyberPower OB fix: rid=0x29 authoritative, rid=0x80 ignored when ac_present=1
- Remove LB false-positive: rid=0x29 bit1 is discharge flag not low-battery
- AJAX portal auto-refresh fixed: sc.className+textContent replaces broken innerHTML
- addOrUpdate row insert fixed: createElement replaces broken innerHTML string
- Poll clock made visible (#aaa color)
- Version strings updated to v15.10 throughout http_portal.c
- Add #include stdio.h to http_compat.c (build fix)
