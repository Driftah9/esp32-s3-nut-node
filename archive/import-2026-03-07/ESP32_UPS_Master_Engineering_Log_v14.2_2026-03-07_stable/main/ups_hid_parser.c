/*============================================================================
 MODULE: ups_hid_parser

 RESPONSIBILITY
 - Decode known APC HID interrupt reports into structured UPS state updates
 - Provide a clean interface between raw USB HID packets and ups_state / NUT

 CURRENT STATE
 - Implements best-effort decoding for the confirmed APC sample reports:
   0x06, 0x0C, 0x13, 0x14, 0x16, 0x21
 - Derives OL/OB/LB from report 0x13 and report 0x16 flags
 - Carries placeholders for battery/input/output voltage and ups.load, which
   will become valid once report-descriptor-driven field mapping is added.

 REVERT HISTORY
 R0  v14.9 parser scaffold

============================================================================*/

#include "ups_hid_parser.h"

#include <string.h>

static uint32_t rd_le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void ups_hid_parser_reset(void)
{
    // Stateless for now; future descriptor-driven mapper can store state here.
}

static void derive_status(ups_state_update_t *upd)
{
    if (!upd) return;

    if (!upd->valid) {
        strlcpy(upd->ups_status, "UNKNOWN", sizeof(upd->ups_status));
        return;
    }

    if (upd->input_utility_present_valid && !upd->input_utility_present) {
        strlcpy(upd->ups_status, "OB", sizeof(upd->ups_status));
        return;
    }

    if (upd->ups_flags_valid && ((upd->ups_flags & 0x00000002U) != 0U)) {
        strlcpy(upd->ups_status, "LB", sizeof(upd->ups_status));
        return;
    }

    strlcpy(upd->ups_status, "OL", sizeof(upd->ups_status));
}

bool ups_hid_parser_decode_report(const uint8_t *data, size_t len, ups_state_update_t *upd)
{
    if (!data || len == 0 || !upd) {
        return false;
    }

    memset(upd, 0, sizeof(*upd));

    const uint8_t rid = data[0];
    bool changed = false;

    switch (rid) {
        case 0x06:
            if (len >= 4U) {
                // Status/housekeeping report; useful for change detection.
                upd->valid = true;
                changed = true;
            }
            break;

        case 0x0C:
            if (len >= 4U) {
                upd->battery_charge_valid = true;
                upd->battery_charge = data[1];

                upd->battery_runtime_valid = true;
                upd->battery_runtime_s = rd_le16(&data[2]);

                upd->valid = true;
                changed = true;
            }
            break;

        case 0x13:
            if (len >= 2U) {
                upd->input_utility_present_valid = true;
                upd->input_utility_present = (data[1] != 0U);

                upd->valid = true;
                changed = true;
            }
            break;

        case 0x14:
            if (len >= 3U) {
                // Currently observed but not yet confidently mapped.
                upd->valid = true;
                changed = true;
            }
            break;

        case 0x16:
            if (len >= 5U) {
                upd->ups_flags_valid = true;
                upd->ups_flags = rd_le32(&data[1]);

                upd->valid = true;
                changed = true;
            }
            break;

        case 0x21:
            if (len >= 2U) {
                // Currently observed but not yet confidently mapped.
                upd->valid = true;
                changed = true;
            }
            break;

        default:
            break;
    }

    if (changed) {
        derive_status(upd);
    }
    return changed;
}
