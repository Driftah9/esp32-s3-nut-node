# Project State — esp32-s3-nut-node

## Last Updated
Date: 2026-03-14 (Session 15 — final)
Updated by: Desktop

---

## Current Status

| Field | Value |
|-------|-------|
| Project Version | v15.10 |
| Overall Status | 🟢 Working |
| Last Action | AJAX auto-refresh fixed — OL/OB updates without F5 |
| Last Action Result | Success |

---

## Last CLI Run

### Command
```powershell
idf.py build flash -p COM3 monitor
```

### Result
- v15.10 confirmed in portal
- AJAX auto-refresh working — OL→OB in ~10s without manual refresh
- Poll clock showing correctly ("5s ago" → "Just updated")
- Both CyberPower and APC OL/OB transitions confirmed

### Outcome
- [x] Build passed
- [x] Flash successful
- [x] AJAX auto-refresh working
- [ ] GitHub push — pending (run .\git-push.ps1)

---

## Current Known Issues

| Issue | Severity | Status |
|-------|----------|--------|
| USB boot-with-device-plugged crash (hub.c:837 assert) | Low | Known/accepted — boot first then plug in |
| App version string shows v15.9-5-ga0f6ea4-dirty | Low | Cosmetic — fix with git tag v15.10 |

---

## Next Recommended Step

1. GitHub push: `.\git-push.ps1` from project root
2. AP password: http://10.0.0.190/config → AP Password (8+ chars)
3. Z2M TCP keepalive: TubeZB coordinator web UI, 30-60s
4. Linux NUT hub (M9): static IP for linuxtest LXC
5. git tag v15.10 to clean up version string
