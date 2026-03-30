/*============================================================================
 MODULE: ups_db_standard

 Standard HID path device entries. These vendors follow HID PDC spec
 closely - only minor quirk flags required, no custom decode logic.
 Grouped in one file (mirrors NUT belkin-hid.c pattern).

 VERSION HISTORY
 R0  v15.17  Extracted from ups_device_db.c.
============================================================================*/
#include "ups_db_standard.h"

static const ups_device_entry_t s_standard_entries[] = {

    /* ---- Tripp Lite (VID 0x09AE) ------------------------------------ */
    /* Some models expose values only via Feature reports.
     * NUT DDL: battery.voltage.nominal=24V, runtime.low=120s
     * US-targeted: input.voltage.nominal=120V */
    {
        .vid         = 0x09AE,
        .pid         = 0,
        .vendor_name = "Tripp Lite",
        .model_hint  = "OMNI/SMART/INTERNETOFFICE",
        .decode_mode = DECODE_STANDARD,
        .quirks      = QUIRK_NEEDS_GET_REPORT,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* ---- Belkin (VID 0x050D) ---------------------------------------- */
    {
        .vid         = 0x050D,
        .pid         = 0,
        .vendor_name = "Belkin",
        .model_hint  = "F6H/F6C Series",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "standby",
    },

    /* ---- Liebert / Vertiv (VID 0x10AF) ------------------------------ */
    /* GXT4/PSI5 double-conversion online UPS.
     * Note: at least two Liebert firmware types share VID:PID 10AF:0001.
     * Newer firmware uses standard HID PDC paths; older firmware has
     * incorrect exponents. Standard path handles both reasonably.
     * NUT DDL: battery.voltage.nominal=24V (conservative default) */
    {
        .vid         = 0x10AF,
        .pid         = 0,
        .vendor_name = "Liebert",
        .model_hint  = "GXT4/PSI5",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "online",
    },

    /* ---- HP (VID 0x03F0) -------------------------------------------- */
    /* T-series G2/G3 line-interactive.
     * NUT DDL values from T750/T1000 G3 confirmed entries.
     * US-targeted: input.voltage.nominal=120V */
    {
        .vid         = 0x03F0,
        .pid         = 0,
        .vendor_name = "HP",
        .model_hint  = "T750/T1000/T1500/T3000 G2/G3",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* ---- Dell (VID 0x047C) ------------------------------------------ */
    {
        .vid         = 0x047C,
        .pid         = 0,
        .vendor_name = "Dell",
        .model_hint  = "H750E/H950E/H1000E/H1750E",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
};

const ups_device_entry_t *ups_db_standard_get_entries(size_t *out_count)
{
    if (out_count) *out_count = sizeof(s_standard_entries) / sizeof(s_standard_entries[0]);
    return s_standard_entries;
}
