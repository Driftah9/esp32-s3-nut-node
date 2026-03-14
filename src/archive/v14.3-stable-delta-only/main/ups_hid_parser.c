/*============================================================================
 MODULE: ups_hid_parser

 RESPONSIBILITY
 - Decode known APC HID interrupt reports into structured UPS state updates
 - Keep currently confirmed fields stable
 - Add guarded candidate tracking for AC voltage experiments
 - Promote input.voltage immediately on first plausible observation for report 0x21
 - Keep output.voltage conservative until a reliable mapping is confirmed

 REVERT HISTORY
 R0  v14.9 parser scaffold
 R1  v14.10 candidate decode pass for battery/load/voltage experiments
 R2  v14.13 v14.3 guarded AC voltage promotion with repeat filtering
 R3  v14.13.1 v14.3.1 first-hit promotion for input.voltage on report 0x21

 NOTES
 - battery.voltage and ups.load remain on the already-proven mappings
 - input.voltage is now promoted on first plausible observation because the
   interrupt reader is change-only and stable values may appear only once
 - output.voltage remains guarded and unpromoted unless repeated plausible
   observations are seen

============================================================================*/

#include "ups_hid_parser.h"

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
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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

static void derive_status(ups_state_update_t *upd)
{
    if (!upd) return;
    if (!upd->valid) { strlcpy(upd->ups_status, "UNKNOWN", sizeof(upd->ups_status)); return; }
    if (upd->input_utility_present_valid && !upd->input_utility_present) {
        strlcpy(upd->ups_status, "OB", sizeof(upd->ups_status)); return;
    }
    if (upd->ups_flags_valid && ((upd->ups_flags & 0x00000002U) != 0U)) {
        strlcpy(upd->ups_status, "LB", sizeof(upd->ups_status)); return;
    }
    strlcpy(upd->ups_status, "OL", sizeof(upd->ups_status));
}

bool ups_hid_parser_decode_report(const uint8_t *data, size_t len, ups_state_update_t *upd)
{
    if (!data || len == 0 || !upd) return false;

    memset(upd, 0, sizeof(*upd));
    const uint8_t rid = data[0];
    bool changed = false;

    switch (rid) {
        case 0x06:
            if (len >= 4U) { upd->valid = true; changed = true; }
            break;

        case 0x0C:
            if (len >= 4U) {
                upd->battery_charge_valid = true;
                upd->battery_charge = data[1];
                upd->battery_runtime_valid = true;
                upd->battery_runtime_s = rd_le16(&data[2]);

                uint32_t raw = rd_le16(&data[2]);
                if (raw >= 800U && raw <= 20000U) {
                    upd->battery_voltage_valid = true;
                    upd->battery_voltage_mv = raw * 10U;
                    ESP_LOGI(TAG, "candidate battery.voltage from report 0x0C raw=%" PRIu32 " => %" PRIu32 " mV", raw, upd->battery_voltage_mv);
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
                uint32_t raw = rd_le16(&data[1]);
                uint32_t mv_candidates[3] = { raw * 1000U, raw * 100U, raw * 10U };
                bool promoted = false;
                for (int i = 0; i < 3; i++) {
                    uint32_t mv = mv_candidates[i];
                    if (is_plausible_ac_mv(mv)) {
                        ESP_LOGI(TAG, "candidate output.voltage from report 0x14 raw=%" PRIu32 " scale[%d] => %" PRIu32 " mV", raw, i, mv);
                        if (tracker_observe(&s_output_tracker, mv)) {
                            upd->output_voltage_valid = true;
                            upd->output_voltage_mv = mv;
                            ESP_LOGI(TAG, "promoted output.voltage from report 0x14 => %" PRIu32 " mV", mv);
                            promoted = true;
                        }
                        break;
                    }
                }
                if (!promoted && raw != 0U)
                    ESP_LOGI(TAG, "report 0x14 raw=%" PRIu32 " not yet plausible for output.voltage", raw);
                upd->valid = true;
                changed = true;
            }
            break;

        case 0x16:
            if (len >= 5U) {
                upd->ups_flags_valid = true;
                upd->ups_flags = rd_le32(&data[1]);

                uint32_t raw = data[1];
                if (raw <= 100U) {
                    upd->ups_load_valid = true;
                    upd->ups_load_pct = (uint8_t)raw;
                    ESP_LOGI(TAG, "candidate ups.load from report 0x16 raw=%" PRIu32 "%%", raw);
                }

                upd->valid = true;
                changed = true;
            }
            break;

        case 0x21:
            if (len >= 2U) {
                uint32_t raw = data[1];
                uint32_t mv_candidates[3] = { raw * 1000U, raw * 10000U, raw * 20000U };
                bool promoted = false;
                for (int i = 0; i < 3; i++) {
                    uint32_t mv = mv_candidates[i];
                    if (is_plausible_ac_mv(mv)) {
                        ESP_LOGI(TAG, "candidate input.voltage from report 0x21 raw=%" PRIu32 " scale[%d] => %" PRIu32 " mV", raw, i, mv);
                        upd->input_voltage_valid = true;
                        upd->input_voltage_mv = mv;
                        ESP_LOGI(TAG, "promoted input.voltage from report 0x21 => %" PRIu32 " mV", mv);
                        promoted = true;
                        break;
                    }
                }
                if (!promoted && raw != 0U)
                    ESP_LOGI(TAG, "report 0x21 raw=%" PRIu32 " not yet plausible for input.voltage", raw);
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
