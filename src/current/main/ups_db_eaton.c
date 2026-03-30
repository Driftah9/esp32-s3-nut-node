/*============================================================================
 MODULE: ups_db_eaton

 Eaton / MGE / Powerware device table entries (VID 0x0463).

 Known PIDs:
   0xFFFF  3S / 5E / 5P / Ellipse / Evolution family (3S 550/700/850 confirmed)
   0x0000  VID-only wildcard fallback (covers remaining Eaton models)

 Eaton 3S HID behaviour (confirmed from two community submissions, 2026-03-30):
   - 926-byte HID descriptor, 111 fields, 10 declared report IDs
   - Descriptor uses vendor page 0xFFFF extensively - all skipped by parser
   - All standard field cache entries (charge, runtime, voltage, status) MISSING
     via the standard HID path - descriptor is not useful for live data
   - 12 undeclared report IDs seen in interrupt-IN stream:
       0x21, 0x22, 0x23, 0x25, 0x28, 0x29 (0x2x range - UPS state data)
       0x80, 0x82, 0x85, 0x86, 0x87, 0x88 (0x8x range - alarm/event)
   - battery.charge confirmed via GET_REPORT rid=0x20:
       response: [0x20, 0x02] -> byte[1] = charge% (2% = nearly depleted)
       polls consistently every 30s, value stable
   - rid=0xFD GET_REPORT returns short read (2 bytes) - not yet decoded
   - NUT subdriver: mge-hid.c (Eaton/MGE HID 1.x)
   - EU-targeted: input.voltage.nominal=230V, battery.voltage.nominal=12V (3S series)

 Open items for future versions:
   - Decode undeclared interrupt-IN rids (0x2x range) for live status/runtime
   - Decode rid=0xFD GET_REPORT (likely runtime or extended status)
   - Map 0x8x interrupt rids to alarm flags

 VERSION HISTORY
 R0  v15.17  Extracted + added confirmed 3S entry with DECODE_EATON_MGE.
============================================================================*/
#include "ups_db_eaton.h"

static const ups_device_entry_t s_eaton_entries[] = {

    /* Eaton 3S / 5E / 5P / Ellipse / Evolution (PID 0xFFFF)
     * Confirmed: 3S 700 (two submissions, 2026-03-30)
     * battery.charge confirmed via GET_REPORT rid=0x20 byte[1]
     * All other fields pending decode of undeclared interrupt-IN rids
     * NUT mge-hid: battery.voltage.nominal=12V for 3S series
     * EU-targeted: input.voltage.nominal=230V */
    {
        .vid         = 0x0463,
        .pid         = 0xFFFF,
        .vendor_name = "Eaton/MGE",
        .model_hint  = "3S/5E/5P/Ellipse/Evolution (PID FFFF)",
        .decode_mode = DECODE_EATON_MGE,
        .quirks      = QUIRK_NEEDS_GET_REPORT,
        .known_good  = true,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 230,
        .ups_type                   = "line-interactive",
    },

    /* VID-only wildcard: other Eaton/MGE/Powerware PIDs
     * Standard HID path - larger Eaton models (9PX, 5PX, Pulsar) use
     * compliant HID descriptors and do not need a custom decode path.
     * NUT DDL: battery.voltage.nominal=24V (mid-range default)
     * EU-targeted: input.voltage.nominal=230V */
    {
        .vid         = 0x0463,
        .pid         = 0,
        .vendor_name = "Eaton/MGE",
        .model_hint  = "3S/5E/5P/Ellipse/Evolution/other",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 230,
        .ups_type                   = "line-interactive",
    },
};

const ups_device_entry_t *ups_db_eaton_get_entries(size_t *out_count)
{
    if (out_count) *out_count = sizeof(s_eaton_entries) / sizeof(s_eaton_entries[0]);
    return s_eaton_entries;
}
