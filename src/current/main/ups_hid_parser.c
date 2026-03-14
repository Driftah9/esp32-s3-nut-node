/*============================================================================
 MODULE: ups_hid_parser

 RESPONSIBILITY
 Decode raw USB HID interrupt-IN reports into structured UPS state updates.

 VERSION HISTORY
 v15.0  Complete rewrite — usage-based (vendor-agnostic).
 v15.3  CyberPower direct-decode bypass (page-filter workaround).
 v15.4  Replace all hardcoded VID:PID logic with ups_device_db lookup.
        Fix derive_status UNKNOWN overwrite bug:
          Previously, each report decoded in isolation — a report that had
          no utility/charge info would derive "UNKNOWN" and overwrite a
          valid "OL" already in g_state.  Fix: derive_status only writes
          "UNKNOWN" into upd->ups_status when it genuinely cannot form
          an opinion; ups_state_apply_update already skips writing status
          if upd->ups_status[0] == 0 — so we clear ups_status on "no
          opinion" instead of writing "UNKNOWN".
 v15.5  APC Back-UPS direct-decode: rid=0C byte0=charge% confirmed.
 v15.6  APC runtime from rid=0C bytes 1-2 (uint16_le, seconds).
        APC BR1000G tested and confirmed — same VID:PID, same decode path.
        cache scan: add uid=0x73 (APC non-standard RunTimeToEmpty)
          and uid=0x67 (RelativeSOC fallback) to battery_runtime and
          battery_charge respectively.
        rid=0C comment updated with confirmed field map from two devices.
 v15.7  Remove input_voltage and output_voltage from cache, standard
        decode path, and CyberPower direct-decode.
        Neither appears via interrupt IN on any tested device —
        APC voltages are Feature-only (GET_REPORT, M-series future task),
        CyberPower rids 0x23 were direct-decoded but data is not needed
        for NUT/HA integration at this time.

 DESIGN
  1. At enumeration: ups_usb_hid calls ups_hid_parser_set_descriptor().
     Device is looked up in ups_device_db.  Quirk flags and decode mode
     are noted.  Field cache is built from parsed descriptor.

  2. On each interrupt-IN report: ups_hid_parser_decode_report() is called.
     - DECODE_CYBERPOWER: direct byte-position decode for known-broken
       CyberPower descriptors.
     - DECODE_STANDARD: generic HID field-cache decode for all other devices.

  3. derive_status builds the NUT compound status string from flags.
     "No opinion" → empty string (caller skips writing to g_state).
     "UNKNOWN" is only written when the parser has data but cannot
     determine on/off-battery state.

 STATUS FLAG RESOLUTION (priority order)
  a) input_utility_present_valid  (most reliable)
  b) discharging_flag / charging_flag
  c) battery_charge_valid (fallback — assume OL if charge known)

 REVERT HISTORY
 R0  v15.0  Initial usage-based rewrite
 R1  v15.3  CyberPower direct-decode bypass
 R2  v15.4  DB-driven device detection; derive_status bug fix
 R3  v15.6  APC runtime from rid=0C; uid=0x73 cache scan
 R4  v15.7  Remove input/output voltage decode and cache entries

============================================================================*/

#include "ups_hid_parser.h"
#include "ups_hid_desc.h"
#include "ups_state.h"
#include "ups_device_db.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "ups_hid_parser";

/* ---- Active device entry from DB ------------------------------------- */
static const ups_device_entry_t *s_device = NULL;

/* ---- Field cache ----------------------------------------------------- */
typedef struct {
    const hid_field_t *battery_charge;
    const hid_field_t *battery_runtime;
    const hid_field_t *battery_voltage;
    const hid_field_t *charging_flag;
    const hid_field_t *discharging_flag;
    const hid_field_t *low_battery_flag;
    const hid_field_t *fully_charged_flag;
    const hid_field_t *fully_discharged_flag;
    const hid_field_t *need_replacement;
    const hid_field_t *input_frequency;
    const hid_field_t *output_frequency;
    const hid_field_t *ups_load;
    const hid_field_t *ups_temperature;
    const hid_field_t *ac_present;
    bool valid;
} hid_field_cache_t;

static hid_field_cache_t s_cache;
static hid_desc_t        s_desc;

/* ----------------------------------------------------------------------- */

void ups_hid_parser_reset(void)
{
    memset(&s_cache, 0, sizeof(s_cache));
    memset(&s_desc,  0, sizeof(s_desc));
    s_device = NULL;
}

/* ----------------------------------------------------------------------- */

void ups_hid_parser_set_descriptor(const hid_desc_t *desc)
{
    if (!desc || !desc->valid) {
        ESP_LOGW(TAG, "set_descriptor: invalid descriptor");
        return;
    }

    ups_hid_parser_reset();
    s_desc = *desc;

    /* ---- Look up device in DB ---- */
    {
        uint16_t vid = 0, pid = 0;
        ups_state_get_vid_pid(&vid, &pid);
        s_device = ups_device_db_lookup(vid, pid);
        ups_device_db_log(s_device, vid, pid);
    }

    /* ---- Apply QUIRK_VENDOR_PAGE_REMAP before building cache ----
       (field scan below uses normalised page numbers)
       Note: remap is applied to the copy in s_desc, not the original.
       usage_page is uint8_t — APC vendor pages 0xFF84/0xFF85 are stored
       as 0x84/0x85 after the descriptor parser already masks the low byte.
       This block is a no-op for current hardware but kept for clarity. */
    if (s_device && (s_device->quirks & QUIRK_VENDOR_PAGE_REMAP)) {
        ESP_LOGI(TAG, "QUIRK_VENDOR_PAGE_REMAP active (APC vendor pages already normalised by descriptor parser)");
    }

    /* ---- Determine if direct-decode is needed ----
       Either the DB says DECODE_CYBERPOWER, OR heuristic: very few Input
       fields on power/battery pages → descriptor is broken, fall back. */
    bool use_direct = (s_device && s_device->decode_mode == DECODE_CYBERPOWER);

    if (!use_direct) {
        uint8_t pd_bs_inputs = 0, other_inputs = 0, rid20 = 0;
        for (uint16_t i = 0; i < s_desc.field_count; i++) {
            const hid_field_t *f = &s_desc.fields[i];
            if (f->report_type != 0) continue;
            if (f->usage_page == HID_PAGE_POWER_DEVICE ||
                f->usage_page == HID_PAGE_BATTERY_SYSTEM) {
                pd_bs_inputs++;
            } else {
                other_inputs++;
            }
            if (f->report_id == 0x20) rid20++;
        }
        if (pd_bs_inputs <= 2 && other_inputs == 0 && rid20 >= 1) {
            use_direct = true;
            ESP_LOGW(TAG, "Heuristic: only %u power/battery Input fields found "
                     "— enabling CyberPower direct-decode as fallback", pd_bs_inputs);
        }
    }

    if (use_direct) {
        ESP_LOGI(TAG, "Decode mode: DIRECT (CyberPower bypass active)");
    } else if (s_device && s_device->decode_mode == DECODE_APC_BACKUPS) {
        ESP_LOGI(TAG, "Decode mode: APC Back-UPS (direct + standard combined)");
    } else {
        ESP_LOGI(TAG, "Decode mode: STANDARD (generic HID descriptor path)");
    }

    /* Store mode back so decode_report can check it */
    /* We abuse s_device — if heuristic forced direct but DB says standard,
       we need a local flag. Use a module-level bool. */
    /* (s_device->decode_mode is const — store separately) */
    /* Simple: just check use_direct via a static flag below */

    /* ---- Build field cache ---- */
    for (uint16_t i = 0; i < s_desc.field_count; i++) {
        const hid_field_t *f = &s_desc.fields[i];
        if (f->report_type != 0) continue;

        uint8_t  pg  = f->usage_page;
        uint16_t uid = f->usage_id;

        if (pg == HID_PAGE_BATTERY_SYSTEM) {
            switch (uid) {
            case HID_USAGE_BS_ABSOLUTESOC:      /* 0x66 RemainingCapacity */
            case HID_USAGE_BS_RELATIVESOC:      /* 0x65 AbsoluteSOC */
            case 0x0064u:                        /* 0x64 RelativeSOC */
            case 0x0067u:                        /* 0x67 RelativeSOC (APC variant) */
                if (!s_cache.battery_charge) s_cache.battery_charge = f;
                break;
            case HID_USAGE_BS_RUNTIMETOEMPTY:   /* 0x68 */
            case 0x0073u:                        /* 0x73 APC non-standard RunTimeToEmpty */
                if (!s_cache.battery_runtime) s_cache.battery_runtime = f;
                break;
            case 0x0083u:
                if (!s_cache.battery_voltage) s_cache.battery_voltage = f;
                break;
            case HID_USAGE_BS_CHARGING:         /* 0x44 */
                if (!s_cache.charging_flag) s_cache.charging_flag = f;
                break;
            case HID_USAGE_BS_DISCHARGING:      /* 0x45 */
                if (!s_cache.discharging_flag) s_cache.discharging_flag = f;
                break;
            case HID_USAGE_BS_BELOWREMCAPLIMIT: /* 0x42 */
                if (!s_cache.low_battery_flag) s_cache.low_battery_flag = f;
                break;
            case HID_USAGE_BS_FULLYCHARGED:     /* 0x46 */
                if (!s_cache.fully_charged_flag) s_cache.fully_charged_flag = f;
                break;
            case HID_USAGE_BS_FULLYDISCHARGED:  /* 0x47 */
                if (!s_cache.fully_discharged_flag) s_cache.fully_discharged_flag = f;
                break;
            case HID_USAGE_BS_NEEDREPLACEMENT:  /* 0x4B */
                if (!s_cache.need_replacement) s_cache.need_replacement = f;
                break;
            case HID_USAGE_BS_ACPRESENT:        /* 0xD0 */
                if (!s_cache.ac_present) s_cache.ac_present = f;
                break;
            default: break;
            }
        } else if (pg == HID_PAGE_POWER_DEVICE) {
            switch (uid) {
            case HID_USAGE_PD_FREQUENCY:        /* 0x32 */
                if (!s_cache.input_frequency)       s_cache.input_frequency  = f;
                else if (!s_cache.output_frequency) s_cache.output_frequency = f;
                break;
            case HID_USAGE_PD_PERCENTLOAD:      /* 0x35 */
                if (!s_cache.ups_load) s_cache.ups_load = f;
                break;
            case HID_USAGE_PD_TEMPERATURE:      /* 0x36 */
                if (!s_cache.ups_temperature) s_cache.ups_temperature = f;
                break;
            case HID_USAGE_PD_ACPRESENT:        /* 0xD0 */
                if (!s_cache.ac_present) s_cache.ac_present = f;
                break;
            default: break;
            }
        }
    }

    s_cache.valid = true;

    /* Log field cache */
    ESP_LOGI(TAG, "Field cache:");
    ESP_LOGI(TAG, "  battery.charge  : %s (rid=%02X)",
             s_cache.battery_charge  ? "found" : "MISSING",
             s_cache.battery_charge  ? s_cache.battery_charge->report_id  : 0xFF);
    ESP_LOGI(TAG, "  battery.runtime : %s (rid=%02X)",
             s_cache.battery_runtime ? "found" : "MISSING",
             s_cache.battery_runtime ? s_cache.battery_runtime->report_id : 0xFF);
    ESP_LOGI(TAG, "  battery.voltage : %s (rid=%02X)",
             s_cache.battery_voltage ? "found" : "MISSING",
             s_cache.battery_voltage ? s_cache.battery_voltage->report_id : 0xFF);
    ESP_LOGI(TAG, "  ups.load        : %s", s_cache.ups_load        ? "found" : "MISSING");
    ESP_LOGI(TAG, "  ac_present      : %s", s_cache.ac_present      ? "found" : "MISSING");
    ESP_LOGI(TAG, "  charging_flag   : %s", s_cache.charging_flag   ? "found" : "MISSING");
    ESP_LOGI(TAG, "  discharging_flag: %s", s_cache.discharging_flag ? "found" : "MISSING");
    ESP_LOGI(TAG, "  low_battery_flag: %s", s_cache.low_battery_flag ? "found" : "MISSING");
    if (use_direct) ESP_LOGI(TAG, "  [direct-decode ACTIVE]");
}

/* ---- Helpers --------------------------------------------------------- */

static bool extract_if_matches(const hid_field_t *field,
                                const uint8_t *data, size_t data_len,
                                uint8_t rid, int32_t *out)
{
    if (!field || field->report_id != rid) return false;
    return ups_hid_desc_extract_field(data, data_len, field, out);
}

/* ---- CyberPower direct-decode ---------------------------------------- */
static bool decode_cyberpower_direct(uint8_t rid,
                                      const uint8_t *p, size_t plen,
                                      ups_state_update_t *upd)
{
    bool changed = false;
    switch (rid) {
    case 0x20:
        if (plen >= 1) {
            uint8_t charge = p[0];
            if (charge <= 100) {
                upd->battery_charge_valid = true;
                upd->battery_charge       = charge;
                changed = true;
                ESP_LOGI(TAG, "[CP] battery.charge=%u%%", charge);
            }
        }
        break;
    case 0x23:
        /* rid=0x23 carried input/output voltage on CyberPower — removed.
         * Voltage decode is not used in current NUT/HA integration.
         * Retained as a no-op case to suppress default: log spam. */
        break;
    case 0x80:
        /*
         * rid=0x80 ac_present — on CP550HG this stays 0x01 even on battery.
         * Only trust it when it says AC is ABSENT (0x00) — that's definitive.
         * When ac_present=1 we ignore it: rid=0x29 is the authoritative
         * source for on-battery state and must not be overwritten.
         */
        if (plen >= 1) {
            bool ac = (p[0] & 0x01u) != 0u;
            if (!ac) {
                /* AC definitely lost — trust this */
                upd->input_utility_present_valid = true;
                upd->input_utility_present       = false;
                changed = true;
            }
            /* ac=1: do not set — rid=0x29 owns this decision */
            ESP_LOGI(TAG, "[CP] ac_present=%u%s", (unsigned)ac,
                     ac ? " (ignored, rid=0x29 authoritative)" : " -> AC LOST");
        }
        break;
    case 0x82:
        if (plen >= 2) {
            uint16_t runtime_s = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
            if (runtime_s < 65000u) {
                upd->battery_runtime_valid = true;
                upd->battery_runtime_s     = runtime_s;
                changed = true;
                ESP_LOGI(TAG, "[CP] battery.runtime=%us", runtime_s);
            }
        }
        break;
    case 0x88:
        if (plen >= 2) {
            uint16_t raw = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
            uint32_t mv  = (uint32_t)raw * 100u;
            if (mv >= 8000u && mv <= 20000u) {
                upd->battery_voltage_valid = true;
                upd->battery_voltage_mv    = mv;
                changed = true;
                ESP_LOGI(TAG, "[CP] battery.voltage=%"PRIu32"mV (raw=%u×100mV)", mv, raw);
            }
        }
        break;
    case 0x85:
        if (plen >= 1 && p[0] != 0x00u) {
            uint32_t flags = 0;
            if (p[0] & 0x01u) flags |= 0x01u;
            if (p[0] & 0x02u) flags |= 0x02u;
            if (p[0] & 0x04u) flags |= 0x04u;
            upd->ups_flags_valid = true;
            upd->ups_flags      |= flags;
            changed = true;
            ESP_LOGI(TAG, "[CP] flags_byte=0x%02X", p[0]);
        }
        break;
    case 0x29:
        /*
         * CyberPower battery/AC status byte.
         * Observed values:
         *   0x00 = on-line (AC present, not discharging)
         *   0x03 = on-battery (discharging — AC lost)
         * This is more reliable than rid=0x80 (ac_present) which
         * stays 0x01 even during discharge on CP550HG.
         * bit0 = discharging, bit1 = low-battery (observed)
         */
        if (plen >= 1) {
            upd->input_utility_present_valid = true;
            upd->input_utility_present       = (p[0] == 0x00u);
            /*
             * 0x03 = on-battery normal discharge (bit0=on-batt, bit1=discharging)
             * 0x02 = low battery observed on other CyberPower models
             * bit1 alone does NOT mean low battery on CP550HG — it
             * appears set whenever discharging. Only flag LB if
             * rid=0x85 or a dedicated low-batt bit indicates it.
             * Leave ups_flags alone here; rid=0x85 handles LB.
             */
            changed = true;
            ESP_LOGI(TAG, "[CP] status_byte=0x%02X -> %s",
                     p[0], (p[0] == 0x00u) ? "OL" : "OB");
        }
        break;
    /* Unresolved reports — silently ignore */
    case 0x21: case 0x22: case 0x25: case 0x28:
    case 0x86: case 0x87:
        break;
    default:
        break;
    }
    return changed;
}

/* ---- APC Back-UPS direct-decode ------------------------------------- */
/*
 * APC Back-UPS (PID 0x0002) live rid map (confirmed from XS 1500M and BR1000G):
 *
 *   rid=06  [3 bytes]  byte0=Charging flag, byte1=Discharging flag,
 *                       byte2=status flags (bit3 observed set at init)
 *                       — handled by standard descriptor path (charging_flag,
 *                         discharging_flag found at rid=06 in both models) ✅
 *   rid=0C  [3 bytes]  byte0 = battery_charge (0–100%) ✅
 *                       byte1:2 = uint16_le = runtime in seconds ✅
 *                         XS 1500M: 0xA0 0x8C → 0x8CA0 = 35,744s (~9.9h)
 *                         BR1000G:  0x60 0x05 → 0x0560 = 1,376s (~23 min)
 *                         (fluctuates as UPS recalculates remaining runtime)
 *   rid=13  [1 byte]   unknown (0x01 observed)
 *   rid=14  [2 bytes]  unknown
 *   rid=16  [4 bytes]  byte0:1 = uint16_le (0x000C=12) — unidentified config
 *   rid=21  [1 byte]   unknown (0x06 observed)
 *
 * VOLTAGES / BATTERY VOLTAGE:
 *   Not present as Input reports in the HID descriptor — likely only
 *   accessible via GET_REPORT (Feature type).  QUIRK_NEEDS_GET_REPORT
 *   is already set in the DB entry; GET_REPORT polling is a future task.
 *
 * STATUS: from rid=06 via standard path (charging/discharging flags) ✅
 * CHARGE: from rid=0C byte0 direct-decode ✅
 * RUNTIME: from rid=0C bytes 1-2 uint16_le direct-decode ✅
 */
static bool decode_apc_backups_direct(uint8_t rid,
                                       const uint8_t *p, size_t plen,
                                       ups_state_update_t *upd)
{
    bool changed = false;
    switch (rid) {
    case 0x0C:
        /*
         * byte0   = battery charge (0–100%)
         * byte1:2 = runtime remaining in seconds (uint16 little-endian)
         * Confirmed on XS 1500M and BR1000G.
         */
        if (plen >= 1) {
            uint8_t charge = p[0];
            if (charge <= 100) {
                upd->battery_charge_valid = true;
                upd->battery_charge       = charge;
                changed = true;
                ESP_LOGI(TAG, "[APC] battery.charge=%u%%", charge);
            }
        }
        if (plen >= 3) {
            uint16_t runtime_s = (uint16_t)(p[1] | ((uint16_t)p[2] << 8));
            /* Sanity: 0 = unknown/not-calculated, cap at ~24h */
            if (runtime_s != 0) {  /* uint16_t max 65535s — no upper cap needed */
                upd->battery_runtime_valid = true;
                upd->battery_runtime_s     = runtime_s;
                changed = true;
                ESP_LOGI(TAG, "[APC] battery.runtime=%us", runtime_s);
            }
        }
        break;
    /* Unresolved reports — silently ignore */
    case 0x13: case 0x14: case 0x16: case 0x21:
        break;
    default:
        break;
    }
    return changed;
}

/* ---- derive_status --------------------------------------------------- */
/*
 * Builds NUT compound status string into upd->ups_status.
 *
 * KEY BEHAVIOUR (v15.4 fix):
 *   - If we cannot determine on/off-battery status from THIS report alone,
 *     we leave upd->ups_status[0] = 0 (empty).
 *   - ups_state_apply_update checks upd->ups_status[0] before writing —
 *     empty means "no opinion this cycle, keep existing g_state.ups_status".
 *   - "UNKNOWN" is only written when we have enough data to conclude that
 *     the UPS state is genuinely unknown (utility unknown AND no charge data).
 *
 * This fixes the bug where rid=0x88 (battery voltage) arrived, changed=true,
 * but had no utility/charge flags → derive_status wrote "UNKNOWN" over the
 * valid "OL" already stored in g_state from the previous rid=0x80 cycle.
 */
static void derive_status(ups_state_update_t *upd)
{
    if (!upd || !upd->valid) {
        /* No data at all — leave empty, do not overwrite g_state */
        return;
    }

    bool on_battery    = false;
    bool utility_known = false;
    bool charging      = false;
    bool discharging   = false;
    bool low_battery   = false;

    if (upd->input_utility_present_valid) {
        on_battery    = !upd->input_utility_present;
        utility_known = true;
    }

    if (upd->ups_flags_valid) {
        charging    = (upd->ups_flags & 0x01u) != 0u;
        discharging = (upd->ups_flags & 0x02u) != 0u;
        low_battery = (upd->ups_flags & 0x04u) != 0u;
        if (!utility_known && discharging) { on_battery = true;  utility_known = true; }
        if (!utility_known && charging)    { on_battery = false; utility_known = true; }
    }

    if (!utility_known) {
        if (upd->battery_charge_valid) {
            /* Charge data only — conservatively assume on-line */
            strlcpy(upd->ups_status, "OL", sizeof(upd->ups_status));
        }
        /* else: no opinion — leave ups_status empty, g_state unchanged */
        return;
    }

    char buf[16];
    if (on_battery) {
        if (low_battery) strlcpy(buf, "OB DISCHRG LB", sizeof(buf));
        else             strlcpy(buf, "OB DISCHRG",    sizeof(buf));
    } else {
        if (low_battery)   strlcpy(buf, "OL LB",   sizeof(buf));
        else if (charging) strlcpy(buf, "OL CHRG", sizeof(buf));
        else               strlcpy(buf, "OL",       sizeof(buf));
    }
    strlcpy(upd->ups_status, buf, sizeof(upd->ups_status));
    ESP_LOGI(TAG, "derive_status -> '%s' (on_battery=%d utility_known=%d charging=%d)",
             buf, (int)on_battery, (int)utility_known, (int)charging);
}

/* ---- Main decode entry point ----------------------------------------- */

bool ups_hid_parser_decode_report(const uint8_t *data, size_t len,
                                   ups_state_update_t *upd)
{
    if (!data || len == 0 || !upd) return false;
    memset(upd, 0, sizeof(*upd));

    if (!s_cache.valid) {
        ESP_LOGW(TAG, "No descriptor loaded");
        return false;
    }

    uint8_t        rid;
    const uint8_t *payload;
    size_t         payload_len;

    if (s_desc.has_report_ids) {
        rid         = data[0];
        payload     = data + 1;
        payload_len = len - 1;
    } else {
        rid         = 0;
        payload     = data;
        payload_len = len;
    }

    bool    changed = false;
    int32_t raw     = 0;

    /* Determine decode mode from DB entry. */
    ups_decode_mode_t mode = s_device ? s_device->decode_mode : DECODE_STANDARD;

    /* Heuristic override: if standard but cache is very sparse (no voltage,
       no runtime, no ac_present) treat as CyberPower-style. */
    if (mode == DECODE_STANDARD &&
        !s_cache.ac_present && !s_cache.battery_runtime && !s_cache.battery_charge) {
        mode = DECODE_CYBERPOWER;
        ESP_LOGD(TAG, "Heuristic: sparse cache — treating as DECODE_CYBERPOWER");
    }

    if (mode == DECODE_CYBERPOWER) {
        if (decode_cyberpower_direct(rid, payload, payload_len, upd)) {
            changed = true;
        }
        if (rid != 0x20) goto finalize;
    } else if (mode == DECODE_APC_BACKUPS) {
        /* APC Back-UPS: direct for vendor rids, then fall through to
           standard path to pick up charging/discharging from descriptor. */
        if (decode_apc_backups_direct(rid, payload, payload_len, upd)) {
            changed = true;
        }
        /* Always fall through to standard path for descriptor fields. */
    }

    /* ---- Standard descriptor path ---- */

    /* Battery Charge */
    if (!upd->battery_charge_valid &&
        extract_if_matches(s_cache.battery_charge, payload, payload_len, rid, &raw)) {
        if (raw >= 0 && raw <= 100) {
            upd->battery_charge_valid = true;
            upd->battery_charge       = (uint8_t)raw;
            changed = true;
            ESP_LOGI(TAG, "battery.charge=%"PRId32"%%", raw);
        }
    }

    /* Battery Runtime */
    if (extract_if_matches(s_cache.battery_runtime, payload, payload_len, rid, &raw)) {
        int8_t  exp     = s_cache.battery_runtime->unit_exponent;
        int32_t seconds = raw;
        if (exp != 0) {
            int64_t r = (int64_t)raw;
            if (exp > 0) for (int i = 0; i < exp  && i < 9; i++) r *= 10;
            else         for (int i = 0; i < -exp && i < 9; i++) r /= 10;
            seconds = (int32_t)r;
        }
        if (seconds >= 0) {
            upd->battery_runtime_valid = true;
            upd->battery_runtime_s     = (uint32_t)seconds;
            changed = true;
            ESP_LOGI(TAG, "battery.runtime=%"PRId32"s", seconds);
        }
    }

    /* Battery Voltage */
    if (!upd->battery_voltage_valid &&
        extract_if_matches(s_cache.battery_voltage, payload, payload_len, rid, &raw)) {
        int32_t mv = 0;
        if (ups_hid_desc_to_milli(raw, s_cache.battery_voltage->unit_exponent, &mv)
            && mv > 0 && mv < 100000) {
            upd->battery_voltage_valid = true;
            upd->battery_voltage_mv    = (uint32_t)mv;
            changed = true;
        } else if (raw > 0 && raw < 100000) {
            upd->battery_voltage_valid = true;
            upd->battery_voltage_mv    = (uint32_t)raw;
            changed = true;
        }
    }

    /* UPS Load */
    if (extract_if_matches(s_cache.ups_load, payload, payload_len, rid, &raw)) {
        if (raw >= 0 && raw <= 100) {
            upd->ups_load_valid = true;
            upd->ups_load_pct   = (uint8_t)raw;
            changed = true;
        }
    }

    /* AC Present */
    if (!upd->input_utility_present_valid &&
        extract_if_matches(s_cache.ac_present, payload, payload_len, rid, &raw)) {
        upd->input_utility_present_valid = true;
        upd->input_utility_present       = (raw != 0);
        changed = true;
    }

    /* Status Flags */
    {
        uint32_t flags   = 0;
        bool     any_flag = false;
        int32_t  fraw    = 0;
        if (extract_if_matches(s_cache.charging_flag,      payload, payload_len, rid, &fraw)) { if (fraw) flags |= 0x01u; any_flag = true; }
        if (extract_if_matches(s_cache.discharging_flag,   payload, payload_len, rid, &fraw)) { if (fraw) flags |= 0x02u; any_flag = true; }
        if (extract_if_matches(s_cache.low_battery_flag,   payload, payload_len, rid, &fraw)) { if (fraw) flags |= 0x04u; any_flag = true; }
        if (extract_if_matches(s_cache.fully_charged_flag, payload, payload_len, rid, &fraw)) { if (fraw) flags |= 0x08u; any_flag = true; }
        if (extract_if_matches(s_cache.need_replacement,   payload, payload_len, rid, &fraw)) { if (fraw) flags |= 0x10u; any_flag = true; }
        if (any_flag) { upd->ups_flags_valid = true; upd->ups_flags = flags; changed = true; }
    }

finalize:
    if (changed) {
        upd->valid = true;
        derive_status(upd);
    }

    return changed;
}
