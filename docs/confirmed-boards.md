# Confirmed Compatible Boards — ESP32-S3 NUT Node

Boards in this list have been physically tested and verified working with this firmware.
Board info is collected as part of every [UPS Compatibility Report](https://github.com/Driftah9/esp32-s3-nut-node/issues/new?template=ups-compatibility-report.yml).

A board is considered confirmed when at least one successful UPS compatibility report has been submitted using it.

---

## Confirmed Working Boards

| Board | Flash | PSRAM | Confirmed by | First seen | Notes |
|-------|-------|-------|--------------|------------|-------|
| Hosyond ESP32-S3-WROOM-1 N16R8 | 16MB | 8MB | @Driftah9 | v15.13 | Primary dev board |
| ESP32-S3-WROOM-1 (generic devkit) | 8MB | 8MB | @omarkhali | v15.15 | Confirmed via issue #1 |

---

## Board Requirements

Any ESP32-S3 board with the following is expected to work:

| Requirement | Minimum | Recommended | Notes |
|-------------|---------|-------------|-------|
| Flash | 4MB | 16MB | 4MB is tight — use a minimal sdkconfig |
| PSRAM | None required | 8MB | Firmware does not currently use PSRAM |
| USB OTG port | Required | Required | Must expose the ESP32-S3 native USB OTG pins |
| USB host power | Required | Required | VBUS must be powered on the OTG port (not all devkits do this by default) |

### Flash size notes

- **4MB** - Fits if you trim logging and disable unused components. Not recommended for production.
- **8MB** - Comfortable. Confirmed working (Omar's board).
- **16MB** - Primary dev and test platform. Most headroom.

### VBUS / OTG power

The ESP32-S3 USB OTG port must supply 5V VBUS to power the UPS USB interface.
Some devkits require a solder jumper or external supply to enable VBUS on the OTG port.
Check your board's schematic if the UPS is not detected at all.

---

## sdkconfig.defaults

`sdkconfig.defaults` is not included in the repo because it is board-specific (flash size, PSRAM config).
Copy the example and edit for your board before building:

```bash
cp src/current/sdkconfig.defaults.example src/current/sdkconfig.defaults
```

See `sdkconfig.defaults.example` for common board configurations including 8MB and 16MB flash variants.

---

*Last updated: 2026-03-17 — v15.15*
