
# ESP32 UPS NUT Node
# Master Engineering Log — Baseline v14.8.1

Hardware: ESP32-S3 USB OTG Host
Framework: ESP‑IDF v5.3.1
UPS Vendor: APC (HID Power Device)

## Confirmed Functional Baseline

System boot, WiFi AP+STA, configuration portal, NUT TCP server,
USB host stack, UPS detection, HID descriptor parsing,
interrupt‑IN report streaming, and disconnect detection are confirmed working.

### Example USB Enumeration Log

USB Device: VID:PID=051d:0002
HID interface: intf=0 alt=0
HID Report Descriptor length = 1049 bytes
Interrupt IN EP=0x81 MPS=8
Interface claimed
Starting interrupt IN reader

### HID Reports Observed

06 00 00 08
0C 64 A0 8C
13 01
14 00 00
16 0C 00 00 00
21 06

These match APC UPS HID behavior used by Network UPS Tools.

## USB Disconnect Behavior

Device removal is correctly detected:

USBH: Device gone
DEV_GONE received
Closing device handle

## Known Issue

After unplugging and reconnecting the UPS, the system does not emit
a new NEW_DEV event.

Root cause likely incomplete cleanup of USB transfers or interface state.

## Milestone Roadmap

M15 – USB Reattach Recovery  
M16 – HID Report Descriptor Parser  
M17 – UPS State Engine  
M18 – NUT Variable Mapping  
M19 – Multi‑UPS Support  
M20 – Portal Enhancements

## Stable Baseline

Version: v14.8.1

Capabilities:
• WiFi portal
• NUT server
• USB HID UPS detection
• Interrupt report streaming
• UPS disconnect detection

Limitation:
USB reattach recovery not implemented yet.
