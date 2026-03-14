# ESP32-S3 APC UPS USB HID → NUT Node

## Master Engineering Log (Source of Truth)

Purpose: This document is the **authoritative engineering record** for
the ESP32-S3 UPS node project. It tracks architecture, milestones,
verified behaviors, and the **exact working firmware** that corresponds
to each milestone.

This document is designed so that: • Any engineer can reproduce the
build • Firmware evolution is clearly tracked • Stable firmware versions
are never lost • Conversations with engineering assistants can resume
with full context

  --------------------------
  FIRMWARE EVOLUTION TABLE
  --------------------------

  ------------------------------------------------------------------------------
  Version   Date         Milestone        Key Achievements          Status
  --------- ------------ ---------------- ------------------------- ------------
  v1        Initial      Architecture     System architecture       Historical
                         planning         defined                   

  v2        Initial      Environment      ESP-IDF configured        Historical
                         setup                                      

  v3        Milestone 1  USB enumeration  UPS VID:PID detected      Stable

  v4        Milestone 2  HID interrupt    Raw interrupt reports     Stable
                         reading          streaming                 

  v5        Milestone 3  Descriptor       HID descriptor length     Stable
                         parsing          discovered                

  v6        Milestone 4  Early decode     Basic report decoding     Stable
                                          started                   

  v7        Milestone 4  Descriptor dump  Experimental              
            refinement   path                                       

  v8        Milestone 5  UPS state model  Stable state tracking +   CURRENT
                                          NUT-style logs            BASELINE

  v9        Planned      Extended HID     Additional report IDs     Planned
                         decode           decoding                  

  v10       Planned      HTTP API         LAN status endpoint       Planned

  v11       Planned      NUT TCP server   Minimal NUT protocol      Planned

  v12       Planned      Web UI           Device configuration      Planned
                                          interface                 
  ------------------------------------------------------------------------------

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

Central Aggregator: Orange Pi Zero 2W IP: 10.0.0.6

  -------------------
  HARDWARE BASELINE
  -------------------

Board: ESP32-S3 development board

Resources: Flash: 16MB PSRAM: 8MB

USB Ports: 1) UART / programming 2) Native OTG

OTG VBUS: Powered via solder bridge from power USB port

UPS Connection Chain:

UPS RJ50 → APC RJ50-to-USB cable → USB-A → USB-C adapter → ESP32 OTG

  ----------------------
  SOFTWARE ENVIRONMENT
  ----------------------

ESP-IDF Version: 5.3.1

Build Commands:

idf.py set-target esp32s3 idf.py build idf.py flash idf.py monitor

  ---------------------
  USB DEVICE BASELINE
  ---------------------

Target UPS: Vendor: APC VID:PID: 051d:0002

USB Parameters:

Speed: FULL HID Interface: 0 Interrupt Endpoint: 0x81 Endpoint MPS: 8
Polling Interval: \~100ms

HID Report Descriptor Length: 1049 bytes

Observed Report IDs:

06 0C 13 14 16 21

  ---------------------------
  CURRENT VERIFIED BASELINE
  ---------------------------

Firmware Version: v8

Confirmed Behaviors:

• USB host installs successfully • APC UPS enumerates reliably • HID
interface 0 claimed • Interrupt IN endpoint streaming works • Reports
logged only when changed • Report decoding implemented: - 0x0C battery
percent + runtime - 0x13 AC present - 0x16 status flags • State model
maintained in firmware • Attach / detach handled cleanly

  --------------
  KNOWN ISSUES
  --------------

• Control GET_DESCRIPTOR(report) may return ESP_ERR_INVALID_ARG •
Descriptor dump currently disabled (non-blocking)

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

When resuming development with a new assistant session, the following
workflow must be followed to maintain continuity.

STEP 1 --- Provide Context Upload or paste the following:

1.  This **Master Engineering Log**
2.  The **current working main.c**
3.  The **latest serial monitor output (optional but recommended)**

STEP 2 --- Declare Current Baseline State clearly:

Current firmware version: main.c CURRENT VERSION: vX

Working milestone: Milestone X

STEP 3 --- Define Next Target State the next milestone objective, for
example:

Goal: Milestone 6 --- Extended HID decoding

STEP 4 --- Preserve Stable Code Before modifying firmware:

• Confirm the current version is stable • Copy the entire working main.c
into the log

STEP 5 --- Implement Changes Make incremental changes only.

Never rewrite working USB host logic unless required.

STEP 6 --- Validate Stability Test:

• USB enumeration • Interface claim • Interrupt streaming • Decode
behavior • Detach / reattach

STEP 7 --- Update Engineering Log When a milestone is confirmed:

1.  Update the Firmware Evolution Table
2.  Document new behaviors
3.  Update Known Issues
4.  Update Next Milestone
5.  Paste the full working main.c

This ensures the log remains the **single source of truth**.

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
