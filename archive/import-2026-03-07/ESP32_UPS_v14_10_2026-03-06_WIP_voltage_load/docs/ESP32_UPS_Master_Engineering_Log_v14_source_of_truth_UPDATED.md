# ESP32 UPS NUT Node — Master Engineering Log (v14, Source of Truth)

> **Project:** ESP32‑S3 USB Host → NUT (upsd) node  
> **Primary goal:** Plug a supported USB UPS into ESP32‑S3, read status via HID/USB, and expose it over NUT `upsd` so standard NUT clients (`upsc`, `upsmon`, etc.) can query it from the LAN.  
> **Maintainer:** Angel Roman II  
> **Last updated:** 2026-03-05 (America/New_York)

---

## 0) Current status (truth as of this log)

### Confirmed working (end-to-end)
- **SoftAP-first provisioning portal** on **192.168.4.1** with routes `/config`, `/save`, `/status`, `/reboot` (raw-socket HTTP implementation).
- **STA join** to LAN (example observed IP: **10.0.0.190**).
- **Plaintext NUT server** on **3493/tcp** interoperates with Linux **`upsc`** from Ubuntu 24.04 LXC:
  - Example query: `upsc ups@10.0.0.190`
  - Example values returned: `battery.charge`, `battery.runtime`, `input.utility.present`, `ups.flags`, `ups.status` (OL).

### Confirmed USB baseline (separate milestone)
- **USB HID attach + HID Report Descriptor dump** for APC UPS VID:PID **051d:0002**, claiming HID interface 0 and reading interrupt IN EP **0x81** MPS 8 (Milestone 2A.1 success).

### In progress
- **Modularization:** keep core orchestrator stable while isolating features into modules.
- **USB UPS module implementation:** current module is a *skeleton*; HID claim + interrupt streaming + decode is next.

---

## 1) Requirements (authoritative)

### 1.1 Device role & networking
- ESP32‑S3 runs an `upsd`-compatible server on **3493/tcp** (initially plaintext).
- Must support:
  - **SoftAP** for first-boot provisioning at **192.168.4.1**
  - **STA** join to local Wi‑Fi (DHCP)
  - LAN target example: **10.0.0.0/16**
- **No baked-in STA SSID/password on first boot.**
- Configuration is entered via portal and stored in NVS.

### 1.2 Web configuration portal
- Must allow setting:
  - STA SSID/password
  - SoftAP SSID/password (WPA2 length rules; blank allowed for open AP if chosen)
  - UPS name (NUT identifier)
  - NUT username/password
- Must show:
  - SoftAP IP, STA IP (if connected)
  - NUT port
  - USB attach state (when implemented)
- Any changes saved must allow reboot (or auto-reboot when appropriate).

### 1.3 USB & UPS data plane
- ESP32‑S3 must operate as **USB Host**, detect hot-plug, and decode UPS values:
  - `battery.charge`
  - `battery.runtime`
  - `ups.status` (OL/OB/LB)
  - `input.utility.present`
  - `ups.flags`
- USB support must not destabilize the confirmed-stable networking/NUT baseline.

---

## 2) Architecture snapshot (current direction)

### 2.1 Modular layout (goal: isolate breakage)
- `main.c` (core orchestrator only)
  - loads config
  - starts Wi‑Fi
  - starts HTTP portal
  - starts NUT server
  - starts USB UPS module
- Modules:
  - `cfg_store.*` — NVS config load/save + defaults
  - `wifi_mgr.*` — AP+STA start, DHCP, STA IP helper
  - `http_portal.*` — raw-socket HTTP portal routes
  - `nut_server.*` — plaintext NUT/upsd-like server
  - `ups_state.*` — shared UPS state model (read by NUT/HTTP)
  - `ups_usb_hid.*` — USB host + HID UPS decode (in progress)

### 2.2 Design rule (important)
- **Each module must include a “mini REVERT block” at the top** describing its responsibility and its last known-good revert state.
- Every distributed ZIP artifact must include these revert blocks for total coverage.

---

## 3) Version history & REVERT index (chronological)

### 3.1 Known-good anchors
- **REVERT-0001 → Firmware v8 (Milestone 2A.1)**  
  USB HID attach + descriptor dump for APC 051d:0002 (USB baseline anchor).
- **REVERT-0002 → Firmware v13.6 “Recovery baseline”**  
  SoftAP-first portal stability anchor (esp_http_server era).
- **REVERT-0003 → Firmware v14.7 “LAN NUT interop baseline” (CONFIRMED)**  
  Raw-socket portal + STA join + NUT plaintext interop confirmed with Linux `upsc`.
- **REVERT-0004 → Firmware v14.7 “modular baseline”**  
  Split v14.7 into modules (no intended behavior change).
- **REVERT-0005 → Firmware v14.7 “modular + USB skeleton”**  
  Adds `ups_state` + `ups_usb_hid` skeleton; NUT reads `ups_state` (demo values until USB decode is implemented).

### 3.2 Notes on v14 failures that were resolved
- Portal save path crashes were caused by:
  - task stack limits (HTTP server stack) and tight-loop parsing behavior.
- v14.7 stability comes from:
  - larger HTTP task stack
  - parser yielding during header reads
  - stable AP+STA provisioning flow

---

## 4) Test plan (must-pass)

### 4.1 Provisioning + LAN reachability
- Fresh device:
  - SoftAP appears, portal reachable at 192.168.4.1
  - Save settings does not crash
  - Reboot transitions to STA and obtains LAN DHCP IP
  - Portal reachable at STA IP

### 4.2 NUT interop sanity
- From LAN Linux host:
  - `upsc ups@<esp-ip>` returns vars (at least the MVP set).
  - Plaintext behavior (no STARTTLS requirement for MVP).

### 4.3 USB attach behavior (next milestone)
- Plug/unplug UPS:
  - device enumerates without deadlock
  - `ups_state` becomes `valid=true` once real HID values are decoded
  - NUT variables reflect real values after updates

---

## 5) Workflow rules (source-of-truth rules)
- Always provide complete “drop-in” files for released anchors.
- Every change set must:
  - update version tag(s)
  - add/maintain REVERT index
  - keep module mini REVERT blocks current
- If a regression happens:
  - identify last REVERT anchor that boots reliably
  - rebuild from there and re-apply changes one at a time

---

## 6) NEXT STEPS (actionable)
1. Implement real USB HID UPS attach in `ups_usb_hid`:
   - detect VID:PID 051d:0002
   - claim HID interface 0
   - open interrupt IN endpoint 0x81
2. Decode at least Report IDs:
   - 0x0C battery charge + runtime
   - 0x13 AC present
   - 0x16 flags
3. Update `ups_state` from decoded values and set `valid=true`.
4. Extend `/status` JSON to show `ups_state.valid` + last update timestamp.
