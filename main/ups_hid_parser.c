/*============================================================================
 MODULE: ups_hid_parser

 RESPONSIBILITY
 - Decode known APC HID interrupt reports into structured UPS state updates
 - Preserve all confirmed v14.3 variable mappings
 - output.voltage investigation: log ALL raw bytes of report 0x14 every time
   it changes so the correct decode scale can be identified from serial output

 REVERT HISTORY
 R0  v14.9 parser scaffold
 R1  v14.10 candidate decode pass for battery/load/voltage experiments
 R2  v14.13 v14.3 guarded AC voltage promotion with repeat filtering
 R3  v14.13.1 v14.3.1 first-hit promotion for input.voltage on report 0x21
 R4  v14.14 WIP - broader scale candidates for 0x14 (did not confirm output.voltage)
 R5  v14.15 restore v14.3.1 stable base + exhaustive raw logging for 0x14
 R6  v14.17 fix: add #include <stdio.h> for snprintf
 R7  v14.18 no connect-time banner (fixes NUT 2.8.1 SSL upsc STARTTLS issue)
 R8  v14.19 multi-model input.voltage + battery.voltage decode
 R9  v14.21 model-aware report 0x0C decode (BR1000G / XS1500M / UNKNOWN)
 R10 v14.22 UNKNOWN path: no promotion until model hint is set
 R11 v14.24 compound ups.status — derive_status() now produces the full
            NUT compound status string that upsmon/HA expect:

            On mains (input_utility_present=1):
              charge=100      -> "OL"
              charge<100      -> "OL CHRG"
              LB flag set     -> "OL LB"    (rare but possible during fast drain)

            On battery (input_utility_present=0):
              normal          -> "OB DISCHRG"
              LB flag set     -> "OB DISCHRG LB"

            No data yet       -> "UNKNOWN"
            USB disconnected  -> "WAIT"  (set by ups_state, not parser)

            ups.flags bit 1 (0x02) = LowBattery in APC HID flag word.
            derive_status() requires at least one of battery_charge_valid
            or input_utility_present_valid to produce a non-UNKNOWN status.

 CONFIRMED VARIABLES (do not regress these):
 - battery.charge        report 0x0C byte[1]
 - battery.runtime       report 0x0C bytes[2:3] LE16 (BR1000G only; raw=seconds)
 - battery.voltage       report 0x0C bytes[2:3] * 10 mV (BR1000G, raw 800-20000)
                                                  * 1 mV  (XS1500M, raw 20000-60000)
 - input.utility.present report 0x13 byte[1] != 0
 - ups.flags             report 0x06 (valid marker) / report 0x16 bytes[1:4] LE32
 - ups.load              report 0x16 byte[1] (when <= 100)
 - input.voltage         report 0x21 byte[1] * 20000 mV (BR1000G raw=6 and XS1500M raw=6)

 UNDER INVESTIGATION:
 - output.voltage   report 0x14 — scale unknown, raw bytes logged
 - battery.runtime  XS1500M — not in report 0x0C, location unknown

============================================================================*/

#include "ups_hid_parser.h"
#include "ups_state.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "ups_hid_parser";

typedef struct {
    bool     valid;
    uint32_t last_mv;
    uint8_t  stable_count;
} ac_candidate_tracker_t;

static ac_candidate_tracker_t s_output_tracker;

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

static bool is_plausible_ac_mv(uint32_t mv)
{
    const uint32_t v = mv / 1000U;
    return ((v >= 90U && v <= 150U) || (v >= 200U && v <= 260U));
}

static bool tracker_observe(ac_candidate_tracker_t *tr, uint32_t mv)
{
    if (!tr) return false;
    if (!is_plausible_ac_mv(mv)) return false;

    if (!tr->valid) {
        tr->valid = true;
        tr->last_mv = mv;
        tr->stable_count = 1;
        return false;
    }

    uint32_t delta = (tr->last_mv > mv) ? (tr->last_mv - mv) : (mv - tr->last_mv);

    if (delta <= 5000U) {
        if (tr->stable_count < 255U) tr->stable_count++;
    } else {
        tr->stable_count = 1;
    }

    tr->last_mv = mv;
    return (tr->stable_count >= 2U);
}

void ups_hid_parser_reset(void)
{
    memset(&s_output_tracker, 0, sizeof(s_output_tracker));
}

/*----------------------------------------------------------------------------
 derive_status — build the compound NUT status string from decoded fields.

 NUT compound status tokens (space-separated, in standard order):
   OL   = On Line (mains present)
   OB   = On Battery
   LB   = Low Battery
   CHRG = Charging
   DISCHRG = Discharging (on battery)

 We only call this when at least one report has been decoded successfully,
 so upd->valid should be true. The function is conservative: if the utility
 present flag hasn't arrived yet, we fall back to OL/OB from the flags word.
----------------------------------------------------------------------------*/
static void derive_status(ups_state_update_t *upd)
{
    if (!upd) return;

    if (!upd->valid) {
        strlcpy(upd->ups_status, "UNKNOWN", sizeof(upd->ups_status));
        return;
    }

    /* Determine on-battery state. Primary: input_utility_present flag.
     * Fallback: APC flags bit 3 (0x08 = OnLine) — if flags present and
     * bit 3 is clear, assume on battery. */
    bool on_battery = false;
    bool utility_known = false;

    if (upd->input_utility_present_valid) {
        on_battery  = !upd->input_utility_present;
        utility_known = true;
    } else if (upd->ups_flags_valid) {
        /* APC flags: bit 3 = OnLine, bit 1 = LowBattery */
        on_battery = ((upd->ups_flags & 0x00000008U) == 0U);
        utility_known = true;
    }

    /* Low battery: APC flags bit 1 (0x02) */
    bool low_battery = upd->ups_flags_valid && ((upd->ups_flags & 0x00000002U) != 0U);

    /* Battery charge — used to determine CHRG.
     * Only meaningful when charge_valid is set. */
    bool charging = false;
    if (!on_battery && upd->battery_charge_valid && upd->battery_charge < 100U) {
        charging = true;
    }

    if (!utility_known) {
        /* Nothing meaningful yet — probably only received report 0x0C so far */
        strlcpy(upd->ups_status, "OL", sizeof(upd->ups_status));
        return;
    }

    /* Build compound status string */
    char buf[16];
    if (on_battery) {
        if (low_battery) {
            strlcpy(buf, "OB DISCHRG LB", sizeof(buf));
        } else {
            strlcpy(buf, "OB DISCHRG", sizeof(buf));
        }
    } else {
        /* On mains */
        if (low_battery) {
            /* LB on mains = battery issue / just returned from deep discharge */
            strlcpy(buf, "OL LB", sizeof(buf));
        } else if (charging) {
            strlcpy(buf, "OL CHRG", sizeof(buf));
        } else {
            strlcpy(buf, "OL", sizeof(buf));
        }
    }

    strlcpy(upd->ups_status, buf, sizeof(upd->ups_status));
}

/* Log all scale candidates for report 0x14 so the correct output.voltage
 * mapping can be identified from serial. Look for [0x14-SCALE] lines. */
static void log_report_0x14_investigation(const uint8_t *data, size_t len)
{
    char hexbuf[64 * 3 + 1];
    int pos = 0;
    int n = (len > 8) ? 8 : (int)len;
    for (int i = 0; i < n; i++) {
        pos += snprintf(&hexbuf[pos], sizeof(hexbuf) - (size_t)pos, "%02X%s",
                        data[i], (i == n - 1) ? "" : " ");
    }
    ESP_LOGI(TAG, "[0x14-RAW] bytes: %s", hexbuf);

    if (len < 3U) return;

    uint32_t raw16 = rd_le16(&data[1]);

    const uint32_t scales[] = { 1000U, 100U, 10U, 20000U, 10000U, 5000U, 2000U };
    for (int i = 0; i < (int)(sizeof(scales) / sizeof(scales[0])); i++) {
        uint32_t mv = raw16 * scales[i];
        if (is_plausible_ac_mv(mv)) {
            ESP_LOGI(TAG, "[0x14-SCALE] raw16=%" PRIu32 " * %" PRIu32 " => %" PRIu32 " mV (%.1f V) PLAUSIBLE",
                     raw16, scales[i], mv, (double)mv / 1000.0);
        }
    }

    if (raw16 == 0U) {
        ESP_LOGI(TAG, "report 0x14 is zero right now (bytes=%02X %02X); no plausible output.voltage candidate yet",
                 data[1], data[2]);
    }
}

bool ups_hid_parser_decode_report(const uint8_t *data, size_t len, ups_state_update_t *upd)
{
    if (!data || len == 0 || !upd) return false;

    memset(upd, 0, sizeof(*upd));
    const uint8_t rid = data[0];
    bool changed = false;

    switch (rid) {
        case 0x06:
            if (len >= 4U) {
                upd->valid = true;
                changed = true;
            }
            break;

        case 0x0C:
            if (len >= 4U) {
                upd->battery_charge_valid = true;
                upd->battery_charge = data[1];

                {
                    uint32_t raw = rd_le16(&data[2]);
                    ups_model_hint_t model = ups_state_get_model_hint();

                    if (model == UPS_MODEL_BR1000G) {
                        upd->battery_runtime_valid = true;
                        upd->battery_runtime_s = raw;

                        if (raw >= 800U && raw <= 20000U) {
                            upd->battery_voltage_valid = true;
                            upd->battery_voltage_mv = raw * 10U;
                            ESP_LOGI(TAG,
                                     "battery.voltage BR1000G raw=%" PRIu32 " => %" PRIu32 " mV",
                                     raw, upd->battery_voltage_mv);
                        }

                    } else if (model == UPS_MODEL_XS1500M) {
                        upd->battery_voltage_valid = true;
                        upd->battery_voltage_mv = raw;
                        ESP_LOGI(TAG,
                                 "battery.voltage XS1500M raw=%" PRIu32 " => %" PRIu32 " mV (%.3f V)",
                                 raw, raw, (double)raw / 1000.0);

                    } else {
                        /* UNKNOWN — do not promote. Early reports arrive before
                         * string fetch completes and model hint is set. */
                        ESP_LOGI(TAG,
                                 "[0x0C-RAW] bytes[2:3]=0x%04" PRIX32 " (%" PRIu32 ") — model UNKNOWN, not promoted",
                                 raw, raw);
                    }
                }

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
                log_report_0x14_investigation(data, len);

                uint32_t raw = rd_le16(&data[1]);
                const uint32_t scales[] = { 1000U, 100U, 10U, 20000U, 10000U, 5000U, 2000U };
                bool promoted = false;
                for (int i = 0; i < (int)(sizeof(scales) / sizeof(scales[0])); i++) {
                    uint32_t mv = raw * scales[i];
                    if (is_plausible_ac_mv(mv)) {
                        if (tracker_observe(&s_output_tracker, mv)) {
                            upd->output_voltage_valid = true;
                            upd->output_voltage_mv = mv;
                            ESP_LOGI(TAG,
                                     "promoted output.voltage from report 0x14 scale=%" PRIu32 " => %" PRIu32 " mV",
                                     scales[i], mv);
                            promoted = true;
                            break;
                        }
                    }
                }
                (void)promoted;

                upd->valid = true;
                changed = true;
            }
            break;

        case 0x16:
            if (len >= 5U) {
                upd->ups_flags_valid = true;
                upd->ups_flags = rd_le32(&data[1]);

                {
                    uint32_t raw = data[1];
                    if (raw <= 100U) {
                        upd->ups_load_valid = true;
                        upd->ups_load_pct = (uint8_t)raw;
                        ESP_LOGI(TAG, "candidate ups.load from report 0x16 raw=%" PRIu32 "%%", raw);
                    }
                }

                upd->valid = true;
                changed = true;
            }
            break;

        case 0x21:
            if (len >= 2U) {
                uint32_t raw = data[1];

                const uint32_t scales[] = { 1000U, 10000U, 20000U, 120000U };
                bool promoted = false;
                for (int i = 0; i < (int)(sizeof(scales) / sizeof(scales[0])); i++) {
                    uint32_t mv = raw * scales[i];
                    if (is_plausible_ac_mv(mv)) {
                        ESP_LOGI(TAG,
                                 "candidate input.voltage from report 0x21 raw=%" PRIu32 " scale[%d]=%" PRIu32 " => %" PRIu32 " mV",
                                 raw, i, scales[i], mv);
                        upd->input_voltage_valid = true;
                        upd->input_voltage_mv = mv;
                        ESP_LOGI(TAG, "promoted input.voltage from report 0x21 => %" PRIu32 " mV", mv);
                        promoted = true;
                        break;
                    }
                }

                if (!promoted && raw != 0U) {
                    ESP_LOGI(TAG, "report 0x21 raw=%" PRIu32 " not yet plausible for input.voltage", raw);
                }

                upd->valid = true;
                changed = true;
            }
            break;

        default:
            break;
    }

    if (changed) derive_status(upd);
    return changed;
}
