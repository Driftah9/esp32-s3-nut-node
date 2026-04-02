# AI_CONTEXT.md - esp32-s3-nut-node
# Ground reference for any AI assistant picking up this project cold.
# Maintained by the primary development session (Claude Desktop).
# Last updated: 2026-04-01, Session 23

---

## What This Project Is

ESP32-S3 firmware that acts as a USB Host for UPS devices (Uninterruptible Power Supplies)
and serves UPS data over the NUT protocol (Network UPS Tools, port 3493).

A NUT client (e.g. NUT on a Proxmox host, Home Assistant, apcupsd) connects to this
device over TCP and queries battery charge, runtime, status etc. The ESP32 bridges the
USB HID UPS protocol to the NUT text protocol.

Hardware: Hosyond ESP32-S3-WROOM-1 N16R8 (16MB flash, 8MB PSRAM)
IDF version: v5.5.3 (tester build) / v5.3.1 (target)
GitHub: https://github.com/Driftah9/esp32-s3-nut-node
Current version: v15.17

---

## Architecture

### USB Path
```
UPS (USB HID) --> ESP32-S3 USB Host --> ups_usb_hid.c (usb_client_task)
                                          |
                                          +--> ups_hid_desc.c   (descriptor parse)
                                          +--> ups_hid_parser.c (interrupt-IN decode)
                                          +--> ups_get_report.c (GET_REPORT polling)
                                          +--> ups_state.c      (unified UPS state)
                                          |
                                          v
                                      nut_server.c (TCP/3493, NUT protocol)
```

### Device Database (v15.17 - vendor-split pattern)
Modeled after NUT's subdriver architecture (apc-hid.c, mge-hid.c, etc.):
- `ups_db_apc.c`        - APC Back-UPS (PID 0002), Smart-UPS (PID 0003), wildcard
- `ups_db_cyberpower.c` - CyberPower PIDs 0501/0601/0005/CyberEnergy, wildcard
- `ups_db_eaton.c`      - Eaton/MGE PID FFFF (confirmed), wildcard
- `ups_db_standard.c`   - Tripp Lite, Belkin, Liebert, HP, Dell (standard HID path)
- `ups_device_db.c`     - Pure coordinator, no table entries, merges via get_entries()

### Decode Modes (ups_device_db.h)
```c
DECODE_STANDARD    = 0  // Standard HID UPS page (page 0x84/0x85) - most devices
DECODE_APC_BACKUPS = 1  // APC Back-UPS: GET_REPORT for voltages, interrupt-IN for status
DECODE_APC_SMARTUPS= 2  // APC Smart-UPS: GET_REPORT rids 0x06/0x0E + interrupt-IN
DECODE_TRIPP_LITE  = 3  // Tripp Lite: GET_REPORT rids 0x01/0x0C
DECODE_EATON_MGE   = 4  // Eaton/MGE: GET_REPORT rids 0x20/0xFD + undocumented INT-IN
```

### Quirk Flags
```c
QUIRK_NEEDS_GET_REPORT = 0x0040  // Device requires Feature report polling (not just INT-IN)
```

### GET_REPORT Architecture (single-owner pattern)
Only `usb_client_task` (in ups_usb_hid.c) may call USB host APIs.
- `gr_timer` task (stack: 4096 bytes) wakes every 30s, posts RID requests to a FreeRTOS queue
- `usb_client_task` calls `ups_get_report_service_queue()` on each event loop iteration
- Service queue drains one request per call, issues control transfer, pumps events, decodes
- This avoids USB concurrency issues - single owner of the client handle

---

## Confirmed Working Devices

| Device | VID:PID | Decode Mode | Status |
|--------|---------|-------------|--------|
| APC Back-UPS (generic) | 051D:0002 | DECODE_APC_BACKUPS | Working - input voltage via GET_REPORT rid=0x17 |
| APC Smart-UPS C 1500 | 051D:0003 | DECODE_APC_SMARTUPS | Working - charging/discharging flags, battery voltage |
| Eaton 3S 700 | 0463:FFFF | DECODE_EATON_MGE | Working - battery.charge + battery.runtime confirmed via interrupt-IN rid=0x06 and GET_REPORT rid=0x20 |

---

## Eaton 3S Status: WORKING (v15.18)

### Confirmed Working (2026-04-02)
- GET_REPORT rid=0x20: returns battery.charge byte, COMPLETED status, ~2s response time
- GET_REPORT rid=0xFD: returns 2 bytes [FD 29], meaning TBD
- Interrupt-IN rid=0x06: fires on mains state change, carries charge + runtime
  - Confirmed: 06 63 B4 10 00 00 -> charge=99%, runtime=4276s on mains loss
- battery.charge=2% was correct (depleted battery from earlier sessions)
- battery.charge=99% confirmed with charged battery
- NUT server stable, LIST VAR responds correctly across 12+ connections

### Fixes applied
1. gr_timer stack 2048->4096 bytes (v15.17) - crash fixed
2. wLength 16->2 bytes for Eaton GET_REPORT (v15.17) - timeout resolved
3. USB_TRANSFER_STATUS_CANCELED spelling fix (v15.18)
4. rid=0x06 interrupt-IN decode added (v15.18) - real-time charge + runtime

### Open items
- ups.status still UNKNOWN - rid=0x06 data[4:5] flags not yet decoded (need OB log)
- rid=0xFD meaning unknown (returns 0x29=41 - possibly runtime in minutes)

### Eaton 3S HID Descriptor Notes
- 111 fields, 10 report IDs, 926 bytes raw
- ALL standard HID field cache entries are MISSING (battery.charge, runtime, voltage etc)
- Vendor page 0x84 only - no standard page 0x85 fields usable
- 12 undocumented interrupt-IN RIDs not in descriptor:
  0x21, 0x22, 0x23, 0x25, 0x28, 0x29, 0x80, 0x82, 0x85, 0x86, 0x87, 0x88
- rid=0x20 in descriptor: type=2 (Feature), size=8, page=84, uid=005A
- rid=0xFD in descriptor: type=2 (Feature), size=16x10, page=84, uid=0094

### XCHK Mechanism (important - do not misinterpret)
The log lines "[XCHK] rid=0xXX seen in interrupt data but NOT in parsed descriptor"
come from a STATIC pre-seeded list of known undocumented rids built from prior
tester submissions - NOT from live runtime capture. These lines will always appear
the same way regardless of what the UPS is currently sending.

---

## Key File Locations (source)

All active source under:
`D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current\main\`

| File | Role |
|------|------|
| ups_get_report.c | GET_REPORT control transfer polling - PRIMARY DEBUG TARGET |
| ups_db_eaton.c | Eaton device DB entry (PID FFFF, quirks, decode mode) |
| ups_hid_desc.c | HID descriptor parser + XCHK cross-check |
| ups_hid_parser.c | Interrupt-IN packet decoder, mode dispatch |
| ups_usb_hid.c | USB host task, interface claim, interrupt-IN reader |
| ups_state.c | Unified UPS state store, NUT variable mapping |
| nut_server.c | TCP NUT protocol server (port 3493) |

---

## Important Implementation Rules

1. **Single USB client owner**: Only usb_client_task calls USB host APIs.
   GET_REPORT uses a queue - timer posts, usb_client_task services. Never call
   usb_host_transfer_submit_control() from any other task.

2. **wLength must match declared report size**: Oversized wLength causes some
   devices (confirmed: Eaton 3S) to STALL or ignore the request entirely.
   Always size to actual declared report length + 1 (rid echo byte).

3. **gr_timer stack**: Must be >= 4096 bytes. 2048 caused stack overflow crash
   before the task could post its first queue entry (fixed v15.17).

4. **Interface claim order**: usb_host_interface_claim() is called before
   GET_REPORT polling starts. This may be relevant to the Eaton timeout bug.

5. **Em dashes forbidden**: Never use em dashes (--) in any file. They corrupt
   to garbage in PowerShell. Use plain hyphen or space-hyphen-space.

6. **Build/flash are manual**: Never attempt idf.py build or flash automatically.
   Stryder runs these manually in the ESP-IDF shell.

---

## NUT Protocol Implementation

- Plaintext NUT protocol on TCP/3493
- No banner mode (some clients reject banner)
- Supported commands: LIST UPS, LIST VAR, GET VAR, LOGIN, USERNAME, PASSWORD, QUIT
- Single UPS presented as "ups" unit
- Variables populated from ups_state.c - only non-zero/confirmed values reported
- battery.charge comes from GET_REPORT rid=0x20 on Eaton, interrupt-IN on APC

---

## Community Testers

- **Eaton tester**: Eaton 3S 700 (0463:FFFF), running v15.17-dirty builds,
  submitting monitor logs. Currently running the expanded logging build.
  Uses idf_monitor on COM7, saves logs to file.

- **Omar**: APC Smart-UPS C 1500 (051D:0003), also working on companion project
  esp32-s3-apc-serial-node (RS-232 serial path for APC Smart-UPS).

---

## Companion Project

`esp32-s3-apc-serial-node` - separate project for APC RS-232 serial protocol.
Fully scaffolded, not yet implemented. Handles APC Smart-UPS via RJ50 connector
and MAX3232 level shifter on GPIO43/44. See that project's AI_CONTEXT.md.

---

## Version History (recent)

| Version | Key Changes |
|---------|-------------|
| v15.14 | USB disconnect/reconnect crash fix (interface_release order) |
| v15.15 | Version string fixes, APC Smart-UPS DB entry fix |
| v15.16 | Various stability fixes, pushed and confirmed working |
| v15.17 | Vendor DB split (apc/cyberpower/eaton/standard files), Eaton 3S support, gr_timer stack fix, wLength fix, expanded GET_REPORT logging |
