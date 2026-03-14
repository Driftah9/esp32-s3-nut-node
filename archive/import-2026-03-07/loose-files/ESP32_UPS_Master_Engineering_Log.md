# ESP32-S3 APC UPS USB HID → NUT Node

## Master Engineering Log (Source of Truth)

Purpose: This document is the **authoritative engineering record** for
the ESP32‑S3 UPS node project. It tracks architecture, milestones,
verified behaviors, and the **exact working firmware** that corresponds
to each milestone.

Every milestone entry MUST include: 1. Environment and hardware state 2.
What was verified working 3. Known issues 4. Next milestone goals 5. The
**complete known‑working main.c**

This ensures the project is reproducible by any engineer at any time.

  ------------------
  PROJECT OVERVIEW
  ------------------

Goal: Build ESP32‑S3 nodes that connect to APC UPS units via USB HID and
expose UPS telemetry to the LAN using a NUT‑compatible protocol and web
interface.

Target Architecture:

UPS (RJ50 HID) ↓ ESP32‑S3 USB OTG Host ↓ UPS Telemetry Decode ↓ Local
State Model ↓ LAN Services • HTTP Status API • NUT Protocol TCP Server
(3493) ↓ Central NUT Aggregator Orange Pi Zero 2W

LAN Range: 10.0.0.0/16

Central Aggregator: Orange Pi Zero 2W IP: 10.0.0.6

  -------------------
  HARDWARE BASELINE
  -------------------

Board: ESP32‑S3 development board

Resources: Flash: 16MB PSRAM: 8MB

USB Ports: 1) UART / programming 2) Native OTG

OTG VBUS: Powered via solder bridge from power USB port

UPS Connection Chain:

UPS RJ50 → APC RJ50‑to‑USB cable → USB‑A → USB‑C adapter → ESP32 OTG

  ----------------------
  SOFTWARE ENVIRONMENT
  ----------------------

ESP‑IDF Version: 5.3.1

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

  --------------------
  MILESTONE TRACKING
  --------------------

Milestone 0 -- Development Environment Milestone 1 -- USB Enumeration
Milestone 2 -- HID Interrupt Reading Milestone 3 -- Change‑only HID
monitoring Milestone 4 -- Basic report decoding Milestone 5 -- UPS state
model Milestone 6 -- Extended decode (planned) Milestone 7 -- HTTP
status endpoint Milestone 8 -- NUT TCP server Milestone 9 -- Web
configuration interface

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
Descriptor dump currently disabled (non‑blocking)

  -----------------------
  NEXT MILESTONE TARGET
  -----------------------

Milestone 6 (Firmware v9)

Goals:

• Add decoding scaffolding for remaining report IDs: - 0x06 - 0x14 -
0x21

• Improve status flag interpretation

• Reduce ESP‑IDF USB debug log noise

• Maintain stable USB enumeration and streaming behavior

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
