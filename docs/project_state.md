# Project State — esp32-s3-nut-node

## Last Updated
Date: 2026-03-14 (CLI Session)
Updated by: CLI

---

## Current Status

| Field | Value |
|-------|-------|
| Project Version | v15.12 |
| Overall Status | 🟢 Verified — flashed and hotplug tested |
| Last Action | Flash + 120s monitor — USB hotplug fix verification |
| Last Action Result | PASS — 2 full disconnect/reconnect cycles, no assert, no crash |

---

## Last CLI Run

### Command
```bash
idf.py flash -p COM3
# then 120s pyserial monitor — docs\monitor.log
```

### Result
- Flash: SUCCESS (exit 0)
- Monitor: 693 lines captured over 120s
- Hotplug cycle 1 (t≈2005s): `Device 1 gone` → graceful cleanup → `Waiting for NEW_DEV` → `Device opened` in ~3.8s
- Hotplug cycle 2 (t≈2019s): Same clean path — reinit in ~7.3s
- No `hub.c:837 assert`, no Guru Meditation, no panic
- NUT server polling active throughout: `[#17] connect/disconnect` from 10.0.0.10
- UPS still showing OB DISCHRG at time of test (running on battery during hotplug test)

### Outcome
- [x] Build passed
- [x] Flash successful
- [x] hub.c:837 hotplug fix VERIFIED — 2 cycles clean
- [ ] GitHub push — pending (run .\git-push.ps1)

---

## Current Known Issues

| Issue | Severity | Status |
|-------|----------|--------|
| IRAM nearly full (99.99%) | Medium | Monitor — may cause issues if code grows |
| App version string dirty tag | Low | Cosmetic — fix with git tag v15.12 after push |

---

## Next Recommended Step

1. GitHub push: `.\git-push.ps1` from project root
2. git tag v15.12 after successful push
