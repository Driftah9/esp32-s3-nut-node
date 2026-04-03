/*============================================================================
 MODULE: ups_db_cyberpower

 CyberPower Systems device table entries (VID 0x0764).

 Known PIDs:
   0x0501  CP consumer series (AVR/SX/ST/CP - CP550HG, SX550G confirmed)
   0x0601  OR/PR rackmount series
   0x0005  Older CP/BC models
   0x0000  VID-only wildcard fallback

 All CyberPower devices use QUIRK_DIRECT_DECODE - the HID descriptor declares
 only ~2 Input fields on power pages; all runtime data arrives on vendor/
 undocumented report IDs (0x20-0x88 range).

 VERSION HISTORY
 R0  v15.17  Extracted from ups_device_db.c.
============================================================================*/
#include "ups_db_cyberpower.h"

static const ups_device_entry_t s_cyberpower_entries[] = {

    /* CyberPower AVR/SX/ST/CP consumer series - confirmed CP550HG, SX550G
     * Decode: direct-decode only (descriptor useless)
     * rids: 0x20=charge, 0x21=runtime, 0x29=status, 0x80=ac_present,
     *       0x85=flags, 0x88=battery_voltage
     * NUT DDL: battery.voltage.nominal=12V, runtime.low=300s
     * US-targeted: input.voltage.nominal=120V */
    {
        .vid         = 0x0764,
        .pid         = 0x0501,
        .vendor_name = "CyberPower",
        .model_hint  = "ST/CP/SX Series (PID 0501)",
        .decode_mode = DECODE_CYBERPOWER,
        .quirks      = QUIRK_DIRECT_DECODE |
                       QUIRK_VOLTAGE_LOGMAX_FIX |
                       QUIRK_BATT_VOLT_SCALE |
                       QUIRK_FREQ_SCALE_0_1,
        .known_good  = true,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* CyberPower OR/PR rackmount + BlueWalker/PowerWalker OEM rebrands.
     * VID 0x0764 is registered to Cyber Power Systems Inc. BlueWalker
     * (PowerWalker brand) uses the same firmware and PID on their VI RLE
     * series - confirmed by NUT HCL issues #645 and #818, and by direct
     * log submission (PowerWalker VI 3000 RLE, 2026-04-02).
     * lsusb identifies these as "Cyber Power System, Inc." regardless of
     * physical branding. Decode path is identical.
     *
     * Confirmed from PowerWalker VI 3000 RLE log (2026-04-02, v15.18):
     *   rid=0x08 [6 bytes]: byte1=charge(0-100%), byte2:3=runtime uint16 LE (seconds)
     *   rid=0x0B [2 bytes]: byte1=status flags (0x13=normal, 0x04=transient - TBD)
     *   NUT driver.version.data: CyberPower HID 0.4
     *
     * Rackmount units typically 24V battery. European units 230V nominal.
     * NUT DDL: battery.voltage.nominal=24V */
    {
        .vid         = 0x0764,
        .pid         = 0x0601,
        .vendor_name = "CyberPower",
        .model_hint  = "OR/PR/RT/UT Series + BlueWalker/PowerWalker OEM (PID 0601)",
        .decode_mode = DECODE_CYBERPOWER,
        .quirks      = QUIRK_DIRECT_DECODE |
                       QUIRK_VOLTAGE_LOGMAX_FIX |
                       QUIRK_BATT_VOLT_SCALE |
                       QUIRK_FREQ_SCALE_0_1 |
                       QUIRK_ACTIVE_PWR_LOGMAX_FIX,
        .known_good  = true,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* Older CyberPower model - standard HID path */
    {
        .vid         = 0x0764,
        .pid         = 0x0005,
        .vendor_name = "CyberPower",
        .model_hint  = "900AVR/BC900D (PID 0005)",
        .decode_mode = DECODE_STANDARD,
        .quirks      = QUIRK_VOLTAGE_LOGMAX_FIX,
        .known_good  = false,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* Cyber Energy / ST Micro OEM */
    {
        .vid         = 0x0483,
        .pid         = 0xa430,
        .vendor_name = "CyberEnergy",
        .model_hint  = "USB Series (ST Micro OEM)",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* VID-only wildcard: any other CyberPower PID */
    {
        .vid         = 0x0764,
        .pid         = 0,
        .vendor_name = "CyberPower",
        .model_hint  = NULL,
        .decode_mode = DECODE_STANDARD,
        .quirks      = QUIRK_VOLTAGE_LOGMAX_FIX | QUIRK_FREQ_SCALE_0_1,
        .known_good  = false,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
};

const ups_device_entry_t *ups_db_cyberpower_get_entries(size_t *out_count)
{
    if (out_count) *out_count = sizeof(s_cyberpower_entries) / sizeof(s_cyberpower_entries[0]);
    return s_cyberpower_entries;
}
