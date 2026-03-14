# Next Milestones — esp32-s3-nut-node

**Based on:** v14.3 stable (2026-03-07)
**Current stable anchor:** REVERT-0006

---

## Milestone v14.4 — output.voltage (NEXT)

**Goal:** Confirm and expose `output.voltage` via NUT.

**What we know:**
- `input.voltage` came from HID Report `0x21` (raw=6, scale=20V → 120.0V)
- `output.voltage` likely lives in a nearby or related report ID
- Has not been observed with a repeatable plausible value yet

**Approach:**
1. Log ALL unhandled HID report IDs + raw bytes to serial during a controlled session
2. Induce a load change or simulate line/battery transition (unplug then replug UPS from wall)
3. Watch for a report field that changes in correlation with expected output voltage (~120V)
4. Once candidate identified: gate with plausibility check (e.g. value between 80–140 after scaling)
5. Only promote to `ups_state` after repeatable confirmed readings
6. Verify end-to-end via `upsc ups@10.0.0.190`

**Do NOT touch:**
- Confirmed stable mappings for any existing 14 variables
- Parser logic for Reports 0x0C, 0x13, 0x16, 0x21

**Success criteria:**
- `output.voltage` appears in `upsc` output with a value matching actual UPS output (~120V)
- Value is stable across multiple poll cycles
- No regression in any currently confirmed variable

---

## Milestone v14.5 — ups.status extended states

**Goal:** Handle OB (On Battery) and LB (Low Battery) status transitions, not just OL.

**What we need:**
- Confirm which HID report field drives OB vs OL distinction
- Confirm LB threshold trigger (likely from battery.charge < threshold or a dedicated flag bit in ups.flags)
- Validate via: unplug UPS from wall → observe `ups.status` transitions to OB

**Success criteria:**
- `ups.status` correctly shows `OB` when UPS is on battery
- `ups.status` shows `LB OB` when battery is low AND on battery
- Transitions back to `OL` when power restored

---

## Milestone v14.6 — upsmon integration (Home Assistant)

**Goal:** Get Home Assistant `upsmon` or NUT integration querying the ESP32 node.

**Steps:**
1. Add ESP32 NUT node to HA NUT integration (`upsc`-compatible)
2. Confirm HA sees all 14 variables
3. Create HA automations:
   - Notify on OB (on battery)
   - Notify on LB (low battery)
   - Optional: trigger graceful shutdown of critical VMs if LB sustained

**Dependencies:**
- v14.4 and v14.5 should be complete first (full variable set + correct status transitions)
- ESP32 must have static DHCP lease (reserve 10.0.0.190 in OPNsense)

---

## Milestone v15.0 — STARTTLS / TLS NUT support (deferred)

**Goal:** Encrypted NUT connection (STARTTLS on 3493).

**Why deferred:**
- Plaintext NUT is LAN-only — acceptable for home network
- TLS adds significant complexity to raw-socket implementation
- Only revisit if HA or another client requires it

---

## Backlog / nice-to-have

| Item | Notes |
|---|---|
| Second UPS model support | Test with different APC model (different report descriptor length) |
| battery.temperature | May exist in HID reports — not investigated yet |
| ups.realpower | Possible from ups.load + input.voltage math |
| OTA firmware update | No plan yet — currently flash via USB cable |
| Static IP option | Currently DHCP only; could add to portal config |
| mDNS / hostname | `esp32-ups.local` for easier discovery |

---

## Rules for all future milestones

- Never break an existing confirmed variable
- Always add to REVERT index when creating a new stable anchor
- WIP builds get REVERT block in each modified module header
- Test each milestone with `upsc ups@<ip>` from Linux NUT client
- Update `HID_REPORT_MAP.md` whenever a new report ID is decoded
