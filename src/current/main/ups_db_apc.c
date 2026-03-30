/*============================================================================
 MODULE: ups_db_apc

 APC / Schneider Electric device table entries (VID 0x051D).

 Known PIDs:
   0x0002  Back-UPS (XS 1500M, BR1000G confirmed)
   0x0003  Smart-UPS C / Smart-UPS (C 1500 confirmed)
   0x0000  VID-only wildcard fallback

 VERSION HISTORY
 R0  v15.17  Extracted from ups_device_db.c.
============================================================================*/
#include "ups_db_apc.h"

static const ups_device_entry_t s_apc_entries[] = {

    /* APC Back-UPS (PID 0x0002) - confirmed XS 1500M and BR1000G
     * Decode: direct rid=0x0C (charge + runtime) + standard (charging flags)
     * GET_REPORT: rid=0x17 for AC line voltage
     * NUT DDL: battery.voltage.nominal=24V, runtime.low=120s */
    {
        .vid         = 0x051D,
        .pid         = 0x0002,
        .vendor_name = "APC",
        .model_hint  = "Back-UPS (PID 0002)",
        .decode_mode = DECODE_APC_BACKUPS,
        .quirks      = QUIRK_VENDOR_PAGE_REMAP | QUIRK_NEEDS_GET_REPORT,
        .known_good  = true,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* APC Smart-UPS C / Smart-UPS (PID 0x0003) - confirmed Smart-UPS C 1500
     * Decode: direct rid=0x07 (status) + rid=0x0D (runtime) + standard (charge)
     * GET_REPORT: rid=0x06 (charging flags) + rid=0x0E (battery voltage)
     * NUT DDL: battery.voltage.nominal=24V, runtime.low=120s */
    {
        .vid         = 0x051D,
        .pid         = 0x0003,
        .vendor_name = "APC",
        .model_hint  = "Smart-UPS C / Smart-UPS (PID 0003)",
        .decode_mode = DECODE_APC_SMARTUPS,
        .quirks      = QUIRK_VENDOR_PAGE_REMAP | QUIRK_NEEDS_GET_REPORT,
        .known_good  = true,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* VID-only wildcard: other APC PIDs - standard HID path */
    {
        .vid         = 0x051D,
        .pid         = 0,
        .vendor_name = "APC",
        .model_hint  = "Smart-UPS / other",
        .decode_mode = DECODE_STANDARD,
        .quirks      = QUIRK_VENDOR_PAGE_REMAP,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
};

const ups_device_entry_t *ups_db_apc_get_entries(size_t *out_count)
{
    if (out_count) *out_count = sizeof(s_apc_entries) / sizeof(s_apc_entries[0]);
    return s_apc_entries;
}
