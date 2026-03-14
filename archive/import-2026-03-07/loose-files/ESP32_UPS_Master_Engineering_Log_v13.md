# ESP32-S3 UPS USB HID → NUT Node

## Master Engineering Log v13 (Source of Truth)

Last Updated: 2026-03-05

------------------------------------------------------------------------

# Purpose

This document is the **authoritative engineering record** for the
ESP32‑S3 UPS Node project.

It ensures:

-   Any engineer can reproduce the build
-   Firmware evolution is preserved
-   Stable firmware versions are never lost
-   Development can resume in new engineering conversations
-   USB behavior and validation history remain documented

------------------------------------------------------------------------

# Firmware Evolution Table

  ----------------------------------------------------------------------------
  Version       Milestone          Key Achievements             Status
  ------------- ------------------ ---------------------------- --------------
  v1            Architecture       Initial architecture defined Historical
                planning                                        

  v2            Environment setup  ESP‑IDF configured           Historical

  v3            USB enumeration    UPS VID/PID detection        Stable

  v4            HID interrupt      Raw reports streaming        Stable
                reader                                          

  v5            Descriptor parsing HID descriptor identified    Stable

  v6            Early decode       Core report IDs identified   Stable

  v7            Descriptor         Dump path experiments        Experimental
                refinement                                      

  v8            State model        UPS state maintained         Stable

  v9            Extended HID       RID tracking + counters      Stable
                decode                                          

  v10           HTTP API           `/status` endpoint           Stable

  v11           NUT TCP server     Minimal NUT protocol         Stable

  v12           LAN mode           DHCP + LAN connectivity      Stable

  v13           Engineering        Requirements + architecture  CURRENT
                baseline expansion expansion                    BASELINE
  ----------------------------------------------------------------------------

------------------------------------------------------------------------

# Project Overview

Goal:

Create ESP32‑S3 nodes that connect to UPS devices via USB HID and expose
telemetry to a LAN using **NUT protocol**.

Architecture:

UPS (USB HID)\
↓\
ESP32‑S3 USB Host\
↓\
Telemetry Decoder\
↓\
UPS State Model\
↓\
Network Services\
• NUT TCP Server (3493)\
• HTTP Status API\
• Web Configuration Interface

------------------------------------------------------------------------

# Hardware Baseline

Board: ESP32‑S3

Resources:

Flash: 16MB\
PSRAM: 8MB

USB:

Native OTG host port

UPS connection chain:

UPS → APC RJ50‑USB cable → USB adapter → ESP32‑S3 OTG

------------------------------------------------------------------------

# Software Environment

ESP‑IDF Version

5.3.1

Build commands:

    idf.py set-target esp32s3
    idf.py build
    idf.py flash
    idf.py monitor

------------------------------------------------------------------------

# Network Architecture

LAN network example:

    10.0.0.0/16

Central NUT server:

    10.0.0.6
    Orange Pi Zero 2W

ESP32 nodes receive DHCP addresses.

Example:

    ups@10.0.0.42

------------------------------------------------------------------------

# Device Requirements

## UPS Functionality

ESP32 must:

-   Act as **NUT node**
-   Scan USB devices dynamically
-   Identify compatible UPS HID devices
-   Extract telemetry values
-   Maintain UPS state model

------------------------------------------------------------------------

# USB Discovery Requirements

Static VID/PID filtering is removed.

Instead:

Device must:

    scan USB HID devices
    identify supported UPS models
    parse report descriptor
    attach driver

Compatibility reference:

Network UPS Tools hardware compatibility list.

------------------------------------------------------------------------

# Networking Modes

## Mode 1 -- Setup Mode

SoftAP enabled.

Default configuration:

    SSID: UPS_NODE_SETUP
    IP: 192.168.4.1

Allows configuration.

------------------------------------------------------------------------

## Mode 2 -- LAN Mode

Device joins local network.

Example:

    DHCP → 10.0.0.x

NUT clients connect normally.

------------------------------------------------------------------------

# Web Configuration Interface

Capabilities:

-   Configure WiFi network
-   Change NUT device identifier
-   Change NUT credentials
-   Change SoftAP password
-   View UPS status
-   Save configuration
-   Reboot device

------------------------------------------------------------------------

# NUT Protocol Implementation

Current supported commands:

    VER
    LIST UPS
    LIST VAR
    GET VAR

Future commands:

    LOGIN
    USERNAME
    PASSWORD
    SET VAR
    INSTCMD

------------------------------------------------------------------------

# Known Issues

  ID       Status     Summary
  -------- ---------- ------------------------------------------
  KI‑001   Open       HID descriptor control transfer unstable
  KI‑002   Observed   Descriptor length mismatch seen once

------------------------------------------------------------------------

# Recommended Validation Tests

## T0 Build Test

Compile firmware and confirm clean build.

## T1 USB Enumeration

Confirm:

-   NEW_DEV event
-   HID interface detected
-   Endpoint claimed

## T2 Streaming

Confirm interrupt reports stream indefinitely.

## T3 Decode

Confirm correct decode:

-   battery.charge
-   battery.runtime
-   input.utility.present

## T4 Reattach

Unplug UPS and reconnect.

Ensure streaming resumes.

## T5 AC Pull

Disconnect AC power and verify:

-   AC state change
-   flag updates

------------------------------------------------------------------------

# Workflow for New Conversations

When resuming development:

1.  Upload this **Master Engineering Log**
2.  Upload current working **main.c**
3.  Provide latest serial monitor output

Then:

-   Declare current firmware version
-   Define next milestone
-   Preserve stable code
-   Implement small incremental changes
-   Run validation tests

------------------------------------------------------------------------

# Next Milestone

Milestone 10

Goal:

USB auto discovery and driver abstraction.

Driver architecture:

    ups_driver_apc_hid
    ups_driver_generic_hid
    ups_driver_cyberpower

------------------------------------------------------------------------

# Future Firmware Roadmap

  Version   Target
  --------- ---------------------------
  v14       NVS configuration storage
  v15       Web UI
  v16       mDNS discovery
  v17       Multi‑UPS support

------------------------------------------------------------------------

# Verified Working Firmware

Current baseline firmware:

    v12

Future revisions should paste the **full verified main.c** here after
milestone confirmation.

------------------------------------------------------------------------

END OF MASTER ENGINEERING LOG
