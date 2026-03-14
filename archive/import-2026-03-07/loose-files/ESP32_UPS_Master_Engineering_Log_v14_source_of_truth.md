# ESP32 UPS NUT Node — Master Engineering Log (v14, Source of Truth)

> **Project:** ESP32‑S3 USB Host → NUT (upsd) node  
> **Primary goal:** Plug a supported USB UPS into ESP32‑S3, read status via HID/USB, and expose it over NUT `upsd` so standard NUT clients (`upsc`, `upsmon`, etc.) can query it from the LAN.  
> **Maintainer:** Angel Roman II  
> **Last updated:** 2026-03-05 (America/New_York)

---

## 0) What is “done” vs “in progress”

### Confirmed working (from project record)
- **USB HID attach + HID Report Descriptor dump** for APC UPS VID:PID **051d:0002**, including claiming HID interface 0 and reading interrupt IN EP **0x81** MPS 8.  
  - This is the **Milestone 2A.1** success (see historic record in v5/v10stable).
- **SoftAP-first configuration portal** boots and serves `/config`, `/status`, `/save`, `/reboot`, with DHCP on **192.168.4.1** and dynamic SSID `ESP32-UPS-SETUP-<MAC>` (confirmed by serial logs during v13 recovery iterations).
- **LAN STA join + DHCP acquisition** to a 10.0.0.0/16 network was observed at least once (example: **10.0.0.190**) with `/config` reachable on that LAN IP (observed during v13 testing).

### Not yet confirmed end-to-end (still pending)
- **End-to-end NUT interoperability with `upsc` from Linux** reliably (no SSL/TLS mismatch, correct protocol behavior, robust auth).  
- **USB UPS autodiscovery** across “any UPS supported by NUT” (HCL-driven matching) beyond the initial APC sample.

---

## 1) Requirements (authoritative)

### 1.1 Device role & networking
- ESP32‑S3 acts as a **NUT node** (runs an `upsd`-compatible TCP server on **3493/tcp**).
- Must support:
  - **SoftAP** for first-boot provisioning at **192.168.4.1**
  - **STA (client) mode** join to local Wi‑Fi
  - **DHCP** on STA network (example LAN range: **10.0.0.0/16**)
- **No baked-in STA SSID/password on first boot**:
  - STA credentials must be entered via SoftAP portal
  - Credentials are saved only **after successful connection test**
  - Provide **“Save + Test”** and **optional reboot** action after changes

### 1.2 USB & UPS data plane
- ESP32‑S3 must:
  - Operate as **USB Host**
  - Detect hot‑plug USB devices
  - Identify UPS devices and **select a driver/decoder path**
  - Pull live values (battery charge, runtime, OL/OB, etc.) and expose them as NUT variables

### 1.3 Web configuration portal (SoftAP + LAN)
- Web UI must allow:
  - Join local Wi‑Fi (STA SSID + password)
  - Change **UPS identifier** so clients can query `upsname@ip` (and in future multi-UPS)
  - Change **NUT username/password** (for NUT authentication)
  - Change **SoftAP password**
  - Show current status (STA connected? IP? UPS attached? last poll? NUT users configured?)
  - Any change that is saved must present a **“Reboot device”** option

### 1.4 Security / auth expectations
- NUT auth:
  - username/password supported
  - minimum viable access control: reject unknown users
- SoftAP password:
  - must meet minimum WPA2 length rules (≥ 8 chars) or explicitly run open AP for first-boot only (decision tracked below)

### 1.5 Interop expectations
- Must interoperate with standard NUT tools on Linux (example test host: Ubuntu 24.04 LXC):
  - `upsc ups@<device-ip>[:3493]`
  - later: `upsmon`, `upscmd`, etc.

---

## 2) Architecture snapshot (target)

### 2.1 Task layout (proposed)
- **usb_task**
  - USB host init
  - device hotplug scan
  - select/attach decoder
  - periodic poll / interrupt-driven updates
- **nut_server_task**
  - TCP server on 3493
  - `upsd`-like parser: `HELP`, `VER`, `LIST UPS`, `LIST VAR <ups>`, `GET VAR <ups> <var>`, auth
  - serve a single “UPS” instance initially
- **wifi_manager**
  - SoftAP always available or “AP fallback”
  - STA connect on saved creds
  - report STA IP + link state
- **httpd_task**
  - `/status` JSON (for UI + debugging)
  - `/config` HTML form
  - `/save` (POST or GET handler)
  - `/reboot`

### 2.2 Persistent configuration (NVS)
- STA creds: ssid/pass
- SoftAP password
- NUT users (at least 1 user)
- UPS name / alias (e.g., `ups`, `Medical`, etc.)
- (future) last-known UPS USB identity + selected driver

---

## 3) Version history & REVERT index (chronological)

> **Intent:** Quickly identify what to roll back to when regressions happen.

### 3.1 Known-good anchors
- **REVERT-0001 → Firmware v8 (Milestone 2A.1)**  
  USB HID attach + descriptor dump for APC 051d:0002 is confirmed working and remains the most reliable USB baseline.
- **REVERT-0002 → Firmware v13.6 “Recovery baseline”**  
  SoftAP-first config portal, no baked-in STA creds, stable boot and portal availability.
  - Verified: SSID appears, portal reachable at 192.168.4.1, NUT port can listen.
  - Note: this is the networking/config stability anchor.

### 3.2 Later experiments (not fully validated end-to-end)
- **v13.12** introduced STARTTLS/SSL related behavior in the NUT server path (record shows Linux `upsc` returning “Init SSL without certificate database” then “Unknown error”); treated as a regression until proven otherwise.
- **v13.13** is intended to **restore plaintext upsd behavior** (remove STARTTLS), while keeping header-length fix and the “GET/POST /save” behavior.  
  - **Status:** build/flash + LAN `upsc` validation still pending in the record.

---

## 4) Key incidents & fixes (from v13 field testing)

### 4.1 HTTP 431: “Header fields are too long”
- Symptom:
  - Browser POST to `/save` fails with **431 Request Header Fields Too Large**
  - Serial shows `request URI/header too long`
- Root cause:
  - `CONFIG_HTTPD_MAX_REQ_HDR_LEN` too small for modern Chrome headers.
- Fix:
  - Raise `CONFIG_HTTPD_MAX_REQ_HDR_LEN` to **2048** (or higher if needed).
  - Also accept `/save` via **GET** (query params) as a fallback.

### 4.2 STA connect + reboot losing reachability
- Symptom:
  - After “Save + reboot”, device disappears (no SSID and no LAN IP response).
- Mitigations implemented/required:
  - Always keep SoftAP available (or implement **AP fallback** if STA connect fails / no IP within timeout).
  - Add portal status page that shows:
    - STA connect state, last disconnect reason, last DHCP IP
  - Ensure “Save + Test” runs STA connect test **before committing** creds, or commits + rollback on failure.

### 4.3 `upsc` SSL initialization / unknown error
- Symptom (Linux):
  - `upsc ups@10.0.0.190:3493` → “Init SSL without certificate database” then “Unknown error”
- Likely cause:
  - Protocol/TLS mismatch (server advertising or behaving like STARTTLS while client expects plain `upsd` unless configured otherwise).
- Fix direction:
  - Ensure server speaks **plain upsd** initially (no STARTTLS) until a full TLS design is implemented and tested.

---

## 5) UPS compatibility strategy (scanning beyond a single VID/PID)

### 5.1 Requirement update (authoritative)
- **Do not hardcode a single UPS VID/PID** (`UPS_VID`, `UPS_PID`) as static compile-time constants.
- Must support **USB scanning** and attach to any UPS that is supported by NUT (as practical), starting with:
  - HID Power Device Class UPS
  - Common vendor HID implementations (APC, CyberPower, Eaton, etc.)

### 5.2 Practical implementation plan
- Phase A (realistic MVP):
  - Enumerate USB devices
  - If HID interface exists with expected usage/pages, treat as UPS candidate
  - Implement a minimal **HID → NUT variable mapping** for commonly available fields
- Phase B (expand):
  - Build a small internal “driver table” keyed by vendor/product and/or HID report signatures
  - Optional: embed a curated subset of NUT HCL entries (not the full HCL HTML)

---

## 6) Test plan (must-pass for “v13 LAN mode complete”)

### 6.1 Boot & provisioning
- Fresh device (after `idf.py erase-flash`):
  - SoftAP comes up with unique SSID and portal reachable on 192.168.4.1
  - `/config` loads in modern Chrome
  - “Save + Test STA Connect” succeeds with valid Wi‑Fi
  - STA obtains DHCP address on 10.0.0.0/16 LAN
  - Portal reachable on LAN IP

### 6.2 NUT protocol sanity
- From LAN Linux host:
  - `upsc -V` works (NUT installed)
  - `upsc ups@<esp-ip>` returns variables:
    - `battery.charge`, `battery.runtime`, `ups.status`, and at least 1 input utility present flag
  - Auth test:
    - invalid user rejected
    - valid user accepted (if client uses authenticated commands later)

### 6.3 USB attach behavior
- Plug/unplug UPS:
  - Device enumerates without deadlock
  - Variables update and persist across reconnects
  - NUT server continues responding during USB events

---

## 7) Current status summary (what to do next)

### 7.1 Stabilize networking + portal
- Keep **REVERT-0002 (v13.6)** behavior as baseline: AP-first, no baked-in STA creds, header length fixed.
- Maintain AP fallback until STA is verified stable across multiple reboots.

### 7.2 Fix/confirm NUT interop
- Validate **v13.13** behavior from a Linux host:
  - Confirm `upsc ups@<ip>` works with **no TLS**
  - Confirm correct protocol responses (`VER`, `HELP`, `LIST UPS`, `LIST VAR`, `GET VAR`)

### 7.3 Expand UPS coverage safely
- Implement USB HID detection + minimal variable extraction for at least:
  - `battery.charge`
  - `battery.runtime`
  - `ups.status` (OL/OB/LB)
  - `input.utility.present`
- Then iterate with additional UPS models.

---

## 8) Notes on style & workflow (source-of-truth rules)
- **Always provide complete “drop-in” `main.c` files** (no snippet-only edits).
- Every change set must update:
  - Version tag in logs
  - This master log (append entry + REVERT anchor if warranted)
- If a regression happens:
  - Identify the last REVERT anchor that boots reliably
  - Rebuild from there and re-apply changes one at a time

---

## 9) NEXT STEPS (actionable)
1. **Rebuild + flash v13.13** (plaintext upsd, large HTTP header limit) and capture boot log.  
2. From LAN Linux host, run:
   - `nc -v <esp-ip> 3493` then `VER`, `LIST UPS`, `LIST VAR ups`, `GET VAR ups ups.status`
   - `upsc ups@<esp-ip>` (no `:3493` needed unless non-default)
3. If `upsc` still errors:
   - capture `tcpdump` on client and add debug logging in server command parser to see where it diverges.
4. Once `upsc` passes, lock **REVERT-0003** as “LAN NUT interop baseline”.

