# ESP32 UPS NUT Node
## Master Engineering Log — v14 — 2026-03-06 — stable

**Project:** ESP32-S3 USB UPS to NUT network bridge  
**Hardware Target:** ESP32-S3 with USB OTG host  
**Framework:** ESP-IDF v5.3.1  
**Current Baseline Status:** stable  
**Date Locked:** 2026-03-06

---

## 1. Baseline Summary

This baseline is now confirmed working end-to-end on hardware.

The system successfully performs:

- SoftAP portal startup and configuration serving
- STA join to the user's LAN
- NUT plaintext server on TCP/3493
- USB host install and client registration
- APC UPS USB attach detection
- HID interface discovery and claim
- HID interrupt-IN report streaming
- USB disconnect detection
- USB reattach recovery without reboot
- HID parser → ups_state → nut_server pipeline
- Live `upsc` reads from a remote Linux machine

This is the first baseline where the USB transport layer, recovery logic, parser path, and NUT exposure are all confirmed working together.

---

## 2. Confirmed Runtime Result

Confirmed from the Linux test machine:

```text
root@linuxtest:~# upsc ups@10.0.0.190
Init SSL without certificate database
battery.charge: 100
battery.runtime: 1324
input.utility.present: 1
ups.flags: 0x0000000C
ups.status: OL
```

This proves the following live values are currently implemented and exposed through NUT:

- `battery.charge`
- `battery.runtime`
- `input.utility.present`
- `ups.flags`
- `ups.status`

---

## 3. Confirmed Working Functional Areas

### 3.1 Wi-Fi / Portal

Working:

- SoftAP launches correctly
- config portal reachable at `http://192.168.4.1/config`
- STA credentials can be saved
- node joins infrastructure LAN successfully
- NUT service becomes reachable on the STA IP

### 3.2 USB Host / HID

Working:

- `usb_host_install()`
- `usb_host_client_register()`
- `NEW_DEV` detection
- USB device open
- active config parsing
- HID interface discovery
- HID report descriptor length discovery
- interrupt IN endpoint discovery
- interface claim
- interrupt report streaming

### 3.3 Disconnect / Reattach

Working:

- UPS unplug is detected
- USB session cleanup runs
- interface is released
- device handle is closed
- session state resets
- second attach is detected cleanly
- streaming resumes after replug

This confirms the reattach recovery fix is operational.

### 3.4 Parser / State / NUT Pipeline

Working:

- raw HID packets feed `ups_hid_parser`
- parser produces structured updates
- `ups_state` stores current values
- `nut_server` serves live values over TCP/3493
- `upsc` reads valid live data from a remote client

---

## 4. Confirmed USB Device Behavior

### APC Back-UPS 1500
Observed characteristics:

- VID:PID = `051d:0002`
- HID report descriptor length = `1049`
- known repeating reports:
  - `06 00 00 08`
  - `0C 64 A0 8C`
  - `13 01`
  - `14 00 00`
  - `16 0C 00 00 00`
  - `21 06`

### APC Back-UPS 1000
Observed characteristics:

- VID:PID = `051d:0002`
- HID report descriptor length = `1133`
- reports differ from the 1500, including:
  - `0C 64 48 05`
  - `0C 64 60 05`
  - `0C 64 78 05`

### Conclusion
USB identification and streaming are working correctly.

Even though both UPS models use the same APC USB VID:PID pair, the node correctly detects that they are different devices because:

- HID report descriptor lengths differ
- live HID report content differs
- decoded runtime/flag behavior changes with the attached UPS

---

## 5. Current NUT Variable Status

### Confirmed working now
These are implemented and verified live:

- `ups.status`
- `battery.charge`
- `battery.runtime`
- `input.utility.present`
- `ups.flags`

### Plumbed but not yet decoded
These fields have state/server support but are not yet populated by the parser:

- `battery.voltage`
- `input.voltage`
- `output.voltage`
- `ups.load`

That means the NUT server side is ready for them, but parser mapping still needs to be added.

---

## 6. Current Module Architecture

Project modules in this baseline:

```text
main/
  main.c
  cfg_store.c
  cfg_store.h
  wifi_mgr.c
  wifi_mgr.h
  http_portal.c
  http_portal.h
  nut_server.c
  nut_server.h
  ups_state.c
  ups_state.h
  ups_usb_hid.c
  ups_usb_hid.h
  ups_hid_parser.c
  ups_hid_parser.h
  CMakeLists.txt
```

### Module responsibilities

**cfg_store**  
Stores and retrieves persistent node configuration.

**wifi_mgr**  
Manages AP+STA startup, SoftAP portal networking, and STA got-IP state.

**http_portal**  
Serves the portal and configuration endpoints.

**ups_usb_hid**  
Owns USB host lifecycle, enumeration, HID interface claim, interrupt IN reads, and reattach recovery.

**ups_hid_parser**  
Decodes known APC HID reports into structured UPS state updates.

**ups_state**  
Shared state container used by parser and NUT server.

**nut_server**  
Plaintext NUT-compatible listener on TCP/3493.

---

## 7. Important Build Notes

This project must be built for:

```text
esp32s3
```

The correct command sequence is:

```text
idf.py fullclean
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py flash
idf.py monitor
```

A previous failure mode occurred when the project was accidentally configured for `esp32` instead of `esp32s3`.  
That produced large cascades of header/type errors that were not caused by the application source itself.

This baseline assumes the target is correctly locked to `esp32s3`.

---

## 8. Confirmed Stable Baseline Files in This Archive

This archive is intended to be a restorable working baseline.

Included:

- full `main/` working source set
- current parser/state/NUT integration
- current USB reattach-capable `ups_usb_hid`
- this engineering log
- supporting README / project notes present in the package

This archive should be treated as the source-of-truth stable baseline before the next parser expansion milestone.

---

## 9. Known Issues / Non-blockers

### 9.1 USB model strings not yet shown
The code currently logs numeric USB string indexes such as:

- `iMfr=1`
- `iProduct=2`
- `iSerial=3`

but does not yet fetch and print the actual manufacturer/product/serial strings.

So model distinction is currently inferred from descriptor length and report behavior, not from printed product strings.

### 9.2 Voltage / load metrics not yet decoded
`battery.voltage`, `input.voltage`, `output.voltage`, and `ups.load` are not yet live.

The state and NUT plumbing are ready, but the parser does not yet map those HID usages.

---

## 10. Revert / Recovery Baseline

This archive is the recommended rollback point.

### Stable rollback label
`v14_2026-03-06_stable`

### What is guaranteed at this baseline
- portal working
- STA working
- NUT reachable
- USB attach working
- USB detach working
- USB reattach working
- parser/state/NUT live path working
- remote `upsc` success confirmed

---

## 11. Next Recommended Milestones

### Milestone 15A — Voltage / Load Decode
Add parser mappings for:

- `battery.voltage`
- `input.voltage`
- `output.voltage`
- `ups.load`

### Milestone 15B — USB Product String Readout
Add USB string descriptor retrieval so logs can show actual model names such as:

- APC Back-UPS 1500
- APC Back-UPS 1000

### Milestone 16 — Descriptor-Driven HID Mapping
Generalize parser behavior so field extraction follows HID usage information instead of only observed packet IDs.

This will improve compatibility across more APC USB UPS models.

---

## 12. Final Baseline Conclusion

This is now a true working platform baseline.

The project has moved beyond USB bring-up and into telemetry expansion.

At this point:

- hardware communication is proven
- hotplug recovery is proven
- transport is stable
- state pipeline is operational
- NUT interoperability is operational

The next development phase is focused on richer HID decoding rather than basic hardware/debug recovery.
