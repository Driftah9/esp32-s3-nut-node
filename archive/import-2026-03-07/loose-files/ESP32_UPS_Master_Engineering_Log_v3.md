# ESP32-S3 APC UPS USB HID → NUT Node

## Master Engineering Log (Source of Truth)

Purpose: This document is the **authoritative engineering record** for
the ESP32-S3 UPS node project. It tracks architecture, milestones,
verified behaviors, and the **exact working firmware** that corresponds
to each milestone.

This document is designed so that: • Any engineer can reproduce the
build\
• Firmware evolution is clearly tracked\
• Stable firmware versions are never lost\
• Conversations with engineering assistants can resume with full
context\
• USB behavior and validation history can be expanded by others over
time

  --------------------------
  FIRMWARE EVOLUTION TABLE
  --------------------------

  --------------------------------------------------------------------------------
  Version   Date         Milestone        Key Achievements          Status
  --------- ------------ ---------------- ------------------------- --------------
  v1        Initial      Architecture     System architecture       Historical
                         planning         defined                   

  v2        Initial      Environment      ESP-IDF configured        Historical
                         setup                                      

  v3        ---          Milestone 1      USB enumeration (UPS      Stable
                                          VID:PID detected)         

  v4        ---          Milestone 2      HID interrupt reading     Stable
                                          (raw reports streaming)   

  v5        ---          Milestone 3      Descriptor parsing        Stable
                                          (report desc length       
                                          discovered)               

  v6        ---          Milestone 4      Early decode (core report Stable
                                          IDs started)              

  v7        2026-03-05   Milestone 4      Descriptor dump path      Experimental
                         refinement       (best-effort /            
                                          experimental)             

  v8        2026-03-05   Milestone 5      UPS state model +         CURRENT
                                          NUT-style status logs     BASELINE

  v9        Planned      Milestone 6      Extended HID decode       Planned
                                          (0x06/0x14/0x21)          

  v10       Planned      Milestone 7      HTTP API (LAN status      Planned
                                          endpoint)                 

  v11       Planned      Milestone 8      NUT TCP server (minimal   Planned
                                          NUT subset)               

  v12       Planned      Milestone 9      Web UI (configuration     Planned
                                          interface)                
  --------------------------------------------------------------------------------

  ------------------
  PROJECT OVERVIEW
  ------------------

Goal: Build ESP32-S3 nodes that connect to APC UPS units via USB HID and
expose UPS telemetry to the LAN using a NUT-compatible protocol and web
interface.

Target Architecture:

UPS (RJ50 HID) ↓ ESP32-S3 USB OTG Host ↓ UPS Telemetry Decode ↓ Local
State Model ↓ LAN Services • HTTP Status API • NUT Protocol TCP Server
(3493) ↓ Central NUT Aggregator Orange Pi Zero 2W

LAN Range: 10.0.0.0/16

Central Aggregator: Orange Pi Zero 2W\
IP: 10.0.0.6

  -------------------
  HARDWARE BASELINE
  -------------------

Board: ESP32-S3 development board

Resources: Flash: 16MB\
PSRAM: 8MB

USB Ports: 1) UART / programming\
2) Native OTG

OTG VBUS: Powered via solder bridge from power USB port

UPS Connection Chain:

UPS RJ50\
→ APC RJ50-to-USB cable\
→ USB-A → USB-C adapter\
→ ESP32 OTG

  ----------------------
  SOFTWARE ENVIRONMENT
  ----------------------

ESP-IDF Version: 5.3.1

Build Commands:

idf.py set-target esp32s3\
idf.py build\
idf.py flash\
idf.py monitor

  ---------------------
  USB DEVICE BASELINE
  ---------------------

Target UPS: Vendor: APC\
VID:PID: 051d:0002

USB Parameters:

Speed: FULL\
HID Interface: 0\
Interrupt Endpoint: 0x81\
Endpoint MPS: 8\
Polling Interval: \~100ms

HID Report Descriptor Length: 1049 bytes

Observed Report IDs (from INT-IN stream):

06\
0C\
13\
14\
16\
21

  ---------------------------
  CURRENT VERIFIED BASELINE
  ---------------------------

Firmware Version: v8

Confirmed Behaviors:

• USB host installs successfully\
• APC UPS enumerates reliably\
• HID interface 0 claimed\
• Interrupt IN endpoint streaming works\
• Reports logged only when changed\
• Report decoding implemented:\
- 0x0C battery percent + runtime\
- 0x13 AC present\
- 0x16 status flags\
• State model maintained in firmware\
• Attach / detach handled cleanly

  ------------------------
  KNOWN ISSUES (TRACKED)
  ------------------------

  -----------------------------------------------------------------------------------------
  ISSUE-ID    First Seen     Status     Summary                  Notes / Workaround
  ----------- -------------- ---------- ------------------------ --------------------------
  KI-001      2026-03-05     Open       Control                  Descriptor dump disabled
                                        GET_DESCRIPTOR(report)   by default; not required
                                        can return               for INT-IN decode.
                                        ESP_ERR_INVALID_ARG      

  KI-002      2026-03-05     Observed   Report descriptor dump   Treat as dump-path edge
                                        length mismatch observed case; avoid using dump
                                        once (1057 vs expected   until fixed.
                                        1049)                    
  -----------------------------------------------------------------------------------------

Add new known issues above. Do not delete old issues---update status and
notes.

Status values: • Open • Mitigated • Fixed • Won't Fix • Needs Repro

  ---------------------------------------------
  USB BEHAVIOR REFERENCE (EXPECTED SEQUENCES)
  ---------------------------------------------

This section documents the **expected USB behavior** for the known-good
baseline. If behavior deviates, treat it as a regression until proven
otherwise.

## A) Expected Attach (Plug-in) Sequence

1)  USB host installed:
    -   "usb_host_install OK"
2)  Client registered:
    -   "usb_host_client_register OK"
3)  Device attached event:
    -   "NEW_DEV received: addr=X"
4)  Device opened and identified:
    -   "Connected VID:PID=051d:0002 ..."
5)  Config parsed:
    -   "Active config wTotalLength=..."
    -   "HID interface intf=0 alt=0 ..."
    -   "HID Report Descriptor length = 1049 bytes ... (from HID
        descriptor 0x21)"
    -   "Interrupt IN EP=0x81 MPS=8"
6)  Interface claimed:
    -   "Interface claimed"
7)  INT-IN reader started:
    -   "Starting interrupt IN reader: EP=0x81 MPS=8 (change-only)"
8)  First reports arrive (change-only):
    -   "HID IN changed (...) ..."
    -   Decodes for 0x0C / 0x13 / 0x16 show up as those frames arrive
9)  Milestone state reset on attach:
    -   "Milestone 5: state reset on attach."

## B) Expected Detach (Unplug) Sequence

1)  Device gone event:
    -   "DEV_GONE received"
2)  In-flight transfer may complete with error status; implementation
    must avoid resubmit loop
3)  Interface released and device closed
4)  State cleared:
    -   "Milestone 5: state cleared on detach."

## C) Expected Report Cadence / Patterns (Baseline Observations)

• Endpoint: 0x81, MPS 8, typical interval \~100ms\
• Reports repeat frequently but are logged only when changed\
• Common RIDs seen in a steady-state loop: - 0x06 (often seen as "06 00
00 08") - 0x0C (battery % + runtime seconds, e.g. "0C 62 D0 89") - 0x13
(AC present, "13 01" on-line or "13 00" on-battery) - 0x14 (often "14 00
00") - 0x16 (flags, 5 bytes total, e.g. "16 0D 00 00 00") - 0x21 (often
"21 06")

## D) Expected AC Pull / Restore Telemetry (Known-good Indicators)

During a deliberate AC pull test (\~20s typical): • 0x13: AC_present
changes 1 → 0\
• 0x16: flags change (example observed: 0x0000000C → 0x0000000A)\
• 0x0C: battery may drop shortly after (e.g., 100% → 99%) and runtime
updates

During AC restore: • 0x13: AC_present changes 0 → 1\
• 0x16: flags change again (example observed: 0x0000000A → 0x0000000D)\
• 0x0C: runtime/battery refresh

NOTE: Flag meanings are TBD; log bit diffs to map over time.

  ----------------------------------------------------------
  RECOMMENDED TEST & VALIDATION PLAN (REGRESSION-PROOFING)
  ----------------------------------------------------------

When implementing any change, run the following tests **in order**.
Record results in the Validation Log section below.

Test Severity Levels: • MUST (required for merging into baseline)\
• SHOULD (recommended)\
• NICE (optional)

## T0 --- Build/Flash/Boot (MUST)

Pass criteria: • Builds cleanly • Flashes cleanly • Boots with expected
banner (version/milestones)

Record: • commit/tag • ESP-IDF version • board/power notes

## T1 --- USB Attach/Enumerate (MUST)

Pass criteria: • NEW_DEV arrives • Device opens • VID:PID matches target
• Config parsing finds HID intf + EP 0x81 MPS 8 • Interface claim
succeeds • INT-IN reader starts

## T2 --- Steady-State Streaming (MUST)

Pass criteria: • INT-IN transfers continue indefinitely • No memory leak
symptoms • No log spam loops • Change-only logs behave as expected

## T3 --- Decode Sanity (MUST)

Pass criteria: • 0x0C decodes batt% and runtime sensibly • 0x13 decodes
AC_present correctly • 0x16 decodes flags (raw u32) correctly • NUT-ish
status block updates only on change

## T4 --- Detach / Reattach (MUST)

Pass criteria: • DEV_GONE handled cleanly • No crash • Re-plug repeats
full attach sequence and resumes streaming

## T5 --- AC Pull / Restore (SHOULD)

Procedure: • Pull AC power (leave load connected) • Wait 20--60 seconds
• Restore AC

Pass criteria: • 0x13 toggles as expected (1→0, then 0→1) • 0x16 changes
(record values) • 0x0C updates (battery/runtime changes may lag)

## T6 --- Long Run (SHOULD)

Procedure: • Run 1--8 hours

Pass criteria: • No stalls • No WDT resets • No USB host errors
accumulating

## T7 --- Feature-Specific Tests (NICE)

Examples: • If adding HTTP: verify endpoint returns JSON and remains
responsive during USB events • If adding NUT TCP: verify basic command
set works while USB streaming continues

  ------------------------------
  VALIDATION LOG (APPEND-ONLY)
  ------------------------------

Use this section to record **test sessions** for historical
traceability. Do not delete entries. Add newest entries at the top.

Template Entry:

\[YYYY-MM-DD\] Validation Run --- Firmware vX --- (commit/tag:
\_\_\_\_\_\_\_\_) Environment: • ESP-IDF: • Board: • UPS cable chain: •
Notes:

Tests: • T0 Build/Flash/Boot: PASS/FAIL (notes) • T1 Attach/Enumerate:
PASS/FAIL (notes) • T2 Streaming: PASS/FAIL (notes) • T3 Decode Sanity:
PASS/FAIL (notes) • T4 Detach/Reattach: PASS/FAIL (notes) • T5 AC
Pull/Restore: PASS/FAIL (notes) • T6 Long Run: PASS/FAIL
(duration/notes) • T7 Feature-specific: PASS/FAIL (notes)

Observed telemetry (optional): • 0x0C: • 0x13: • 0x16: • Other RIDs:
Known issues observed: • KI-\_\_\_ : Result: • Accept as baseline?
YES/NO

  -----------------------
  NEXT MILESTONE TARGET
  -----------------------

Milestone 6 (Firmware v9)

Goals:

• Add decoding scaffolding for remaining report IDs: - 0x06 - 0x14 -
0x21

• Improve status flag interpretation

• Reduce ESP-IDF USB debug log noise

• Maintain stable USB enumeration and streaming behavior

  -------------------------------------------
  WORKFLOW WHEN STARTING A NEW CONVERSATION
  -------------------------------------------

When resuming development with a new assistant session, use the
following workflow.

STEP 1 --- Provide Context Upload or paste: 1) This **Master Engineering
Log** 2) The **current working main.c** 3) Latest serial monitor output
(recommended)

STEP 2 --- Declare Current Baseline State clearly: • main.c CURRENT
VERSION: vX • Current milestone: Milestone X • What is confirmed working
(one paragraph)

STEP 3 --- Define Next Target State the next milestone objective (what
"done" means).

STEP 4 --- Preserve Stable Code Before modifying firmware: • Confirm
current version is stable • Copy the entire verified working main.c into
this log

STEP 5 --- Implement Changes Incrementally • Avoid rewriting working USB
plumbing unless required • Prefer small diffs that are easy to revert

STEP 6 --- Validate Stability (Run the MUST tests) • T0, T1, T2, T3, T4
are required

STEP 7 --- Update Master Log When a milestone is confirmed: 1) Update
the Firmware Evolution Table 2) Update Current Verified Baseline section
3) Update Known Issues table 4) Add a Validation Log entry 5) Paste the
full working main.c

  ---------------------------
  VERIFIED WORKING FIRMWARE
  ---------------------------

main.c CURRENT VERSION: v8

IMPORTANT: Paste the complete **verified working main.c** here whenever
a milestone is confirmed successful.

This guarantees reproducibility and prevents regression loss.

  -----------------------------
  PASTE VERIFIED main.c BELOW
  -----------------------------

  -------------------------------
  END OF MASTER ENGINEERING LOG
  -------------------------------
