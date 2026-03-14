
# ESP32 UPS Master Engineering Log
Version: v14.1
Date: 2026-03-07
Status: WIP (extended telemetry)

---

## Baseline Summary

This version builds on the **v14 stable baseline** where the ESP32‑S3 NUT node successfully:

- Detects APC UPS via USB HID
- Reads HID interrupt reports
- Parses core UPS status data
- Serves NUT protocol over TCP/3493
- Provides Wi‑Fi AP+STA configuration portal
- Successfully queried using `upsc` from a Linux client

Example verified output:

battery.charge: 100
battery.runtime: ~1400
input.utility.present: 1
ups.flags: 0x0000000C
ups.status: OL
battery.voltage: 14.000
ups.load: 12

---

# Architecture Overview

Core modules:

main.c
 ├── wifi_mgr
 ├── http_portal
 ├── cfg_store
 ├── ups_usb_hid
 │    └── ups_hid_parser
 ├── ups_state
 └── nut_server

Pipeline:

USB HID reports
      ↓
ups_usb_hid
      ↓
ups_hid_parser
      ↓
ups_state
      ↓
nut_server
      ↓
NUT client (upsc)

---

# Confirmed Working Variables

battery.charge
battery.runtime
battery.voltage
ups.load
input.utility.present
ups.flags
ups.status

---

# Variables In Progress

input.voltage
output.voltage

---

# Confirmed Hardware

Tested UPS:
APC Back‑UPS series (USB HID)
VID:PID = 051d:0002

ESP Hardware:
ESP32‑S3 USB OTG capable board

ESP‑IDF Version:
v5.3.1

---

# Network Architecture

SoftAP: 192.168.4.1
STA: DHCP (example LAN: 10.0.0.190)

NUT Server:
tcp/3493
plaintext driver

Example query:
upsc ups@10.0.0.190

---

# Next Milestones

Milestone M15
Extended HID telemetry

Goals:
- input.voltage
- output.voltage
- improved runtime accuracy

Milestone M16
Descriptor‑driven HID parsing

Milestone M17
UPS event detection

---

# Rollback Index

v14   stable baseline
v14.1 telemetry expansion (WIP)

---
