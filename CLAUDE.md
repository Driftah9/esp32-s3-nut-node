# Project Rules — esp32-s3-nut-node
# Type: ESP32 / IDF Firmware

> This file is read by Claude Code CLI when opening this project.
> It defines project-type specific behaviour for ESP32/IDF firmware projects.
> Read after the global CLAUDE.md at D:\Users\Stryder\Documents\Claude\.claude\CLAUDE.md

---

## Project Identity

| Field | Value |
|-------|-------|
| Project Name | esp32-s3-nut-node |
| Target Device | ESP32-S3 (Hosyond ESP32-S3-WROOM-1 N16R8 -- 16MB flash, 8MB PSRAM) |
| IDF Version | v5.3.1 |
| COM Port | COM3 |
| Flash Size | 16MB |
| GitHub Repo | https://github.com/Driftah9/esp32-s3-nut-node |

---

## Source Location

Active source is always at:
```
D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current\
```
All IDF commands must be run from this directory.

---

## Build / Flash / Monitor Workflow

### 1 — Build
```powershell
cd D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current
idf.py build
```
- Capture full output
- Report all errors and warnings
- Do not proceed to flash if build fails
- Fix errors, rebuild, confirm clean before flashing

### 2 — Flash
```powershell
idf.py flash -p COM3
```
- Confirm successful flash before running monitor
- If flash fails — check COM port, check device is in flash mode

### 3 — Monitor (Timed)
```powershell
$job = Start-Job { cd D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current; idf.py monitor 2>&1 }
Start-Sleep -Seconds 60
Stop-Job $job
Receive-Job $job | Tee-Object -FilePath "D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\docs\monitor.log"
Remove-Job $job
```
- Default monitor duration: **60 seconds**
- Increase duration if logs are cutting off before relevant output
- Always write output to `docs\monitor.log`
- After monitor completes — read `docs\monitor.log` and summarise findings

### 4 — Update State
After every build/flash/monitor cycle, update `docs\project_state.md` with results.

---

## File Integrity Checks

Run at the start of every session:

| File | Backup | Action if Missing |
|------|--------|-------------------|
| `src\current\sdkconfig.defaults` | `backups\sdkconfig.defaults.bak` | Restore from backup, notify Stryder |

---

## Versioning

- Current version: v15.8
- Follow sub-versioning rules from `Claude_communication_preference.md`
- Every source file gets a version comment block at the top
- Milestone backups go in `Version\`
- Superseded files go in `src\archive\`

---

## Git

- Use `git-push.ps1` at project root for all GitHub pushes
- Before pushing, ensure `docs\github_push.md` is current
- Claude updates `docs\github_push.md` automatically whenever code changes
- Script auto-detects ESP32 projects via presence of `src\current\`
- Script tags with version number from `docs\github_push.md`
- Script detects first push vs subsequent commits automatically
- Script shows full summary and requires Y/N confirmation before pushing

---

## Rules

- Never flash without a successful build first
- Never edit files in `src\archive\` or `Version\`
- Always confirm COM port with Stryder if flashing fails
- Stryder has final say before any flash operation
