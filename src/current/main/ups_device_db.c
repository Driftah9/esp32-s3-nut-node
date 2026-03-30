/*============================================================================
 MODULE: ups_device_db

 RESPONSIBILITY
 Device database coordinator - merges vendor tables and provides lookup.

 DESIGN
 Each vendor has its own .c/.h file. This coordinator includes all vendor
 tables and walks them in priority order during lookup. Adding a new vendor
 requires only a new ups_db_<vendor>.c/.h and a line here.

 Vendor file split rationale (mirrors NUT usbhid-ups subdriver pattern):
   ups_db_apc.c        VID 0x051D - two confirmed PIDs, distinct decode modes
   ups_db_cyberpower.c VID 0x0764 - QUIRK_DIRECT_DECODE, completely non-standard
   ups_db_eaton.c      VID 0x0463 - DECODE_EATON_MGE, undeclared RID decode
   ups_db_standard.c   All others - standard HID + minor quirks only
                                    (mirrors NUT belkin-hid.c grouping approach)

 Match priority per lookup call:
   1. Exact VID:PID match (any vendor file)
   2. VID-only match   (pid == 0 in table)
   3. Generic sentinel fallback

 VERSION HISTORY
 R0  v15.4  Initial - VID:PID table with quirk flags.
 R1  v15.12 Added NUT static fields to all entries.
 R2  v15.17 Split into per-vendor files. Coordinator now merges tables.
            Added DECODE_EATON_MGE for Eaton 3S (PID FFFF, confirmed).

============================================================================*/
#include "ups_device_db.h"
#include "ups_db_apc.h"
#include "ups_db_cyberpower.h"
#include "ups_db_eaton.h"
#include "ups_db_standard.h"

#include "esp_log.h"
#include <stddef.h>

static const char *TAG = "ups_device_db";

/* ---- Generic sentinel - must remain here, always last in lookup ------ */
static const ups_device_entry_t s_sentinel = {
    .vid         = 0,
    .pid         = 0,
    .vendor_name = "Unknown",
    .model_hint  = "Generic HID UPS",
    .decode_mode = DECODE_STANDARD,
    .quirks      = 0,
    .known_good  = false,
    .battery_voltage_nominal_mv = 0,
    .battery_runtime_low_s      = 120,
    .battery_charge_low         = 10,
    .battery_charge_warning     = 50,
    .input_voltage_nominal_v    = 0,
    .ups_type                   = NULL,
};

/* ---- Vendor table registry ------------------------------------------- */
typedef struct {
    const ups_device_entry_t *(*get_fn)(size_t *);
} vendor_source_t;

static const vendor_source_t s_sources[] = {
    { ups_db_apc_get_entries        },
    { ups_db_cyberpower_get_entries },
    { ups_db_eaton_get_entries      },
    { ups_db_standard_get_entries   },
};

#define NUM_SOURCES (sizeof(s_sources) / sizeof(s_sources[0]))

/* ----------------------------------------------------------------------- */

const ups_device_entry_t *ups_device_db_lookup(uint16_t vid, uint16_t pid)
{
    const ups_device_entry_t *vid_only = NULL;

    for (size_t s = 0; s < NUM_SOURCES; s++) {
        size_t count = 0;
        const ups_device_entry_t *tbl = s_sources[s].get_fn(&count);
        if (!tbl) continue;

        for (size_t i = 0; i < count; i++) {
            const ups_device_entry_t *e = &tbl[i];
            if (e->vid != vid) continue;
            if (e->pid == pid)             return e;        /* exact match */
            if (e->pid == 0 && !vid_only)  vid_only = e;   /* VID-only */
        }
    }

    return vid_only ? vid_only : &s_sentinel;
}

void ups_device_db_log(const ups_device_entry_t *entry, uint16_t vid, uint16_t pid)
{
    if (!entry) return;

    if (entry->vid == 0) {
        ESP_LOGW(TAG, "VID:PID=%04X:%04X - UNKNOWN device. "
                 "Attempting generic HID standard path. "
                 "Add to ups_db_<vendor>.c if this device works.",
                 vid, pid);
        return;
    }

    const char *path_str = "standard";
    switch (entry->decode_mode) {
    case DECODE_CYBERPOWER:   path_str = "direct-decode (CyberPower)"; break;
    case DECODE_APC_BACKUPS:  path_str = "direct-decode (APC Back-UPS)"; break;
    case DECODE_APC_SMARTUPS: path_str = "direct-decode (APC Smart-UPS)"; break;
    case DECODE_EATON_MGE:    path_str = "direct INT-IN undocumented rids (Eaton/MGE)"; break;
    default:                  path_str = "standard"; break;
    }

    if (entry->known_good) {
        ESP_LOGI(TAG, "VID:PID=%04X:%04X - %s %s [known-good, %s path, quirks=0x%04"PRIx32"]",
                 vid, pid,
                 entry->vendor_name,
                 entry->model_hint ? entry->model_hint : "",
                 path_str,
                 (uint32_t)entry->quirks);
    } else {
        ESP_LOGW(TAG, "VID:PID=%04X:%04X - %s %s [not confirmed, %s path, quirks=0x%04"PRIx32"] "
                 "- may work, feedback welcome",
                 vid, pid,
                 entry->vendor_name,
                 entry->model_hint ? entry->model_hint : "",
                 path_str,
                 (uint32_t)entry->quirks);
    }
}
