# BUILD-IGNORE.md — esp32-s3-nut-node
<!-- v1.0 | 2026-03-07 -->

## Build-generated content in src/current/

When compiled via ESP-IDF (idf.py build), the following are auto-generated
and should be ignored by Claude — do not manage, move, or include in reviews:

```
src/current/build/              ← full build output, binaries, .elf, .bin
src/current/.espressif/         ← toolchain cache (if local)
src/current/managed_components/ ← idf component manager downloads
src/current/dependencies.lock   ← component version lock (auto-updated)
src/current/sdkconfig           ← generated config (edit via menuconfig only)
src/current/sdkconfig.old       ← previous sdkconfig backup
src/current/.vscode/            ← VSCode / ESP-IDF extension settings
src/current/.ccls-cache/        ← clangd index cache
```

## Source files Claude manages (in src/current/main/)

These are the ONLY files Claude reads, edits, or creates:

```
main/main.c
main/cfg_store.c / .h
main/wifi_mgr.c / .h
main/http_portal.c / .h
main/nut_server.c / .h
main/ups_state.c / .h
main/ups_usb_hid.c / .h
main/ups_hid_parser.c / .h
main/CMakeLists.txt
```

Project-level build files (read-only reference, not edited by Claude):
```
src/current/CMakeLists.txt      ← top-level project cmake
src/current/sdkconfig           ← read for reference only
```

## Build + Flash workflow (Stryder runs these)

```powershell
# From src/current/ directory in ESP-IDF 5.3 PowerShell:
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor
# Or combined:
idf.py -p COM3 flash monitor
```

## Session handoff note

When starting a new session on this project, Claude should:
1. Read src/current/main/ source files only
2. Ignore build/ and any generated artifacts
3. Check docs/MILESTONES.md for current status
4. Check docs/REVERT-INDEX.md for last confirmed anchor
