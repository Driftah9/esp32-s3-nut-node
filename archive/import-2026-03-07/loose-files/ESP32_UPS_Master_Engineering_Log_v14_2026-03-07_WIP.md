# ESP32 UPS Master Engineering Log
Version: v14 branch
Date: 2026-03-07
Status: WIP

## Executive summary

This log captures the current state of the ESP32-S3 UPS NUT node effort after the modular v14 work stream.

The project has progressed from a USB-host detection proof point into a working modular ESP32-based NUT node that can:
- boot reliably on ESP-IDF v5.3.1
- run Wi-Fi AP+STA
- host a config/status HTTP portal
- enumerate and identify supported APC USB HID UPS devices
- parse several live HID reports
- export a meaningful subset of UPS telemetry to NUT clients

The current branch also includes exploratory work toward descriptor-driven HID mapping for fields not yet confirmed from the interrupt report stream.

At the current stopping point:
- the USB host path is working
- HID interrupt report parsing is working
- the HTTP portal/status endpoint is working
- multiple core NUT variables were previously confirmed working from Linux `upsc`
- descriptor-driven mapping work is in progress
- the current NUT server path appears to need regression cleanup because the latest run showed `upsc` disconnecting while the HTTP endpoint remained healthy

## Confirmed major successes

### 1. USB host baseline established
A confirmed milestone was reached where the ESP32-S3:
- detects APC UPS VID:PID `051d:0002`
- opens the device
- claims HID interface 0
- detects the interrupt IN endpoint
- fetches and reports HID report descriptor length

This proved the USB host stack, attachment flow, and HID interface discovery were all working on the target hardware.

### 2. Modular architecture established
The project now uses a modular structure with dedicated files for:
- app entry
- Wi-Fi management
- portal/server config storage
- USB HID handling
- HID parsing
- UPS state
- NUT protocol serving

### 3. Wi-Fi AP+STA working
The node successfully:
- starts a SoftAP for configuration
- joins the configured STA network
- obtains a LAN IP
- remains reachable over HTTP on the STA IP

Confirmed example:
- `sta_ip`: `10.0.0.190`

### 4. HTTP portal/status path working
The `/status` endpoint remained reachable and reported expected metadata such as:
- AP SSID
- STA SSID
- STA IP
- UPS name
- NUT port

### 5. APC USB identity extraction working
USB string extraction was successfully added and confirmed. The node now reads and stores:
- manufacturer string
- product string
- serial string

Confirmed example from logs:
- `device.mfr`: `American Power Conversion`
- `device.model`: `Back-UPS BR1000G FW:868.L2 .D USB FW:L2`
- `device.serial`: `3B1145X11360`

This also enabled useful NUT metadata fields:
- `device.mfr`
- `device.model`
- `device.serial`
- `driver.name`
- `ups.mfr`
- `ups.model`

### 6. HID interrupt report reader working
The HID interrupt IN path is functioning and repeatedly reports change-only packets. Confirmed live report IDs include:
- `0x06`
- `0x0C`
- `0x13`
- `0x14`
- `0x16`
- `0x21`

### 7. Confirmed parsed UPS telemetry
The parser and state pipeline successfully identified and exported several fields.

Confirmed working fields from earlier Linux validation:
- `battery.charge`
- `battery.runtime`
- `input.utility.present`
- `ups.flags`
- `ups.status`
- `battery.voltage`
- `ups.load`
- `input.voltage`
- `device.mfr`
- `device.model`
- `device.serial`
- `driver.name`
- `ups.mfr`
- `ups.model`

### 8. Input voltage successfully confirmed
A major parser success was confirming:
- `input.voltage`

This was promoted from report `0x21` using a plausible scaling path, with logs showing:
- raw = `6`
- promoted result = `120000 mV`
- exported as `120.0`

### 9. UPS attach / detach / reattach handling improved
The USB device-gone cleanup path was improved enough to allow:
- unplug detection
- cleanup
- reattach detection
- resume of HID reading

## Confirmed field set previously working from Linux

A previously confirmed good `upsc` response included these variables:

- `battery.charge: 100`
- `battery.runtime: 1400`
- `input.utility.present: 1`
- `ups.flags: 0x0000000C`
- `ups.status: OL`
- `battery.voltage: 14.000`
- `input.voltage: 120.0`
- `ups.load: 12`
- `device.mfr: American Power Conversion`
- `device.model: Back-UPS BR1000G FW:868.L2 .D USB FW:L2`
- `device.serial: 3B1145X11360`
- `driver.name: esp32-nut-hid`
- `ups.mfr: American Power Conversion`
- `ups.model: Back-UPS BR1000G FW:868.L2 .D USB FW:L2`

## Active file set in use

### Root
- `CMakeLists.txt`
- `sdkconfig`
- `sdkconfig.old`
- `README.md`
- `LICENSE`
- `dependencies.lock`

### Main application folder
- `main/CMakeLists.txt`
- `main/main.c`
- `main/cfg_store.c`
- `main/cfg_store.h`
- `main/wifi_mgr.c`
- `main/wifi_mgr.h`
- `main/http_portal.c`
- `main/http_portal.h`
- `main/nut_server.c`
- `main/nut_server.h`
- `main/ups_state.c`
- `main/ups_state.h`
- `main/ups_usb_hid.c`
- `main/ups_usb_hid.h`
- `main/ups_hid_parser.c`
- `main/ups_hid_parser.h`
- `main/ups_hid_descriptor.c`
- `main/ups_hid_descriptor.h`

## High-level role of each active module

### `main.c`
Application entry point. Starts the modular subsystems and prints high-level startup banners.

### `cfg_store.[ch]`
Stores and retrieves persistent configuration such as SSID/password and UPS name settings.

### `wifi_mgr.[ch]`
Handles SoftAP + STA mode, AP IP setup, Wi-Fi events, and current STA IP tracking.

### `http_portal.[ch]`
Hosts the HTTP config/status portal. The `/status` endpoint is currently a known good health indicator.

### `nut_server.[ch]`
Implements the NUT plaintext server on TCP/3493 and exports variables from `ups_state`.

### `ups_state.[ch]`
Central in-memory source of truth for decoded UPS telemetry and identity values.

### `ups_usb_hid.[ch]`
Handles:
- USB host initialization
- attach/detach lifecycle
- device identity
- interface claim
- HID report descriptor length discovery
- interrupt IN reader
- handoff to parser/state

### `ups_hid_parser.[ch]`
Interprets raw HID interrupt reports into structured UPS values.

### `ups_hid_descriptor.[ch]`
New descriptor-walk artifact intended to support descriptor-driven mapping of unresolved fields, especially `output.voltage`.

## Known unresolved items

### 1. `output.voltage` still not confirmed
This remains the biggest open telemetry gap.

Observations so far:
- report `0x14` repeatedly appeared as `14 00 00`
- no plausible output-voltage value was exposed from the current interrupt-change stream
- state-transfer testing did not reveal a hidden output-voltage report

Current best engineering conclusion:
- `output.voltage` is probably not available from the interrupt stream alone
- it likely requires descriptor-driven mapping and possibly feature-report access

### 2. Descriptor-driven path not yet fully integrated
Artifacts for descriptor walking were created, but the descriptor-driven path is not yet fully producing the expected `hid_desc_map` logs in the running firmware.

### 3. NUT server regression in latest stop point
At the latest stopping point:
- HTTP `/status` worked
- STA IP remained valid
- USB/HID parsing still worked
- Linux `upsc ups@10.0.0.190` failed with server disconnect

That suggests the current active problem is likely in:
- `nut_server.c`
or
- the server task / accept / response path

rather than Wi-Fi or USB host.

## Known failures and friction encountered

### Build-system / path issues
Several build problems were encountered and resolved or partially worked around, including:
- `build.ninja` dirty-loop failures in some Windows paths
- project folder naming/path issues
- configuration churn after moving the project path
- build delays that were initially mistaken for hangs

Moving to a simpler path such as `C:\esp\upsnode` helped significantly.

### Config mismatch / USB host enable friction
There was confusion around USB host enablement and `sdkconfig`, including cases where:
- the build completed
- but runtime logs still reported USB host as disabled

This was eventually pushed forward into working USB host behavior.

### Header / type dependency issues
Examples included:
- `app_cfg_t` visibility problems in `wifi_mgr`
- IP helper type mismatches
- follow-on include-order / build churn

These were resolved enough to restore successful builds.

### Wrong host helper usage for descriptor fetch
A descriptor-fetch attempt used:
- `usb_control_get_descriptor()`

which does not exist in the current ESP-IDF host API being used. This caused compile failures and required a shift toward proper control-transfer based fetching.

### Current NUT connectivity regression
Latest observed state:
- `upsc` disconnects
- HTTP status still works
- USB HID parsing continues

## Best-known branch state at pause time

### Working
- build/flash/monitor workflow
- Wi-Fi AP+STA
- HTTP status/config portal
- APC USB identification
- USB HID interrupt reading
- parser for several core fields
- `input.voltage`
- identity/metadata export
- reattach handling

### Not yet working or not yet proven
- `output.voltage`
- descriptor-driven field mapping output
- latest NUT server stability

## Milestone progression summary

### Early baseline
- USB host attach and HID descriptor-length discovery proved viable

### Modular baseline
- project split into modules
- Wi-Fi and HTTP portal brought online

### NUT export milestone
- core telemetry exported successfully to Linux `upsc`

### Metadata milestone
- manufacturer/product/serial strings extracted and exported

### Input-voltage milestone
- `input.voltage` validated and exported

### Output-voltage investigation milestone
- transfer-event tests and report probing did not reveal a valid interrupt-stream source

### Descriptor-driven milestone start
- `ups_hid_descriptor` artifacts created
- integration work started but not yet completed into a stable observable flow

## Recommended next steps when work resumes

1. Restore NUT server stability first
   - verify listener accept/respond path
   - re-establish known-good `upsc` output

2. Reconfirm the v14 stable baseline after NUT fix
   - USB
   - parser
   - HTTP
   - NUT

3. Complete descriptor integration
   - fetch HID report descriptor correctly through supported host-control transfer path
   - run `ups_hid_descriptor` logger
   - identify Power Device / Battery System usages

4. Pursue descriptor/object-based `output.voltage`
   - do not continue blind scale guessing from `0x14`
   - only promote after descriptor evidence

## Rollback guidance

The best rollback concept at this point is:
- keep the modular v14 architecture
- keep the confirmed parser mappings for:
  - battery.charge
  - battery.runtime
  - battery.voltage
  - input.utility.present
  - input.voltage
  - ups.load
  - ups.status
  - metadata exports
- treat descriptor-driven work and the latest NUT regression as active WIP

## Final status statement

This project is well beyond proof-of-concept.

It already demonstrates a functional ESP32-S3 UPS NUT node with:
- real APC HID detection
- real parsed UPS data
- real NUT-compatible variable export history
- real HTTP/device management support

The primary engineering risks remaining are now concentrated in:
- robust NUT server stability
- descriptor-driven expansion for unresolved HID fields
- especially `output.voltage`

That is a strong place to pause and resume from.
