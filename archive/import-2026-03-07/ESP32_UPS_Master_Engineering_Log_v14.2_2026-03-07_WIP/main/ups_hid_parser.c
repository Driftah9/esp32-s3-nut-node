/*============================================================================
 MODULE: ups_hid_parser

 RESPONSIBILITY
 - Decode known APC HID interrupt reports into structured UPS state updates
 - Add cautious candidate mappings for additional voltage/load fields
 - Log candidate mappings only when they are plausible

 REVERT HISTORY
 R0  v14.9 parser scaffold
 R1  v14.10 candidate decode pass for battery/input/output/load

============================================================================*/

#include "ups_hid_parser.h"
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"

static const char *TAG = "ups_hid_parser";

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

                upd->battery_runtime_valid = true;
                upd->battery_runtime_s = rd_le16(&data[2]);

                uint32_t raw = rd_le16(&data[2]);
                if (raw >= 800U && raw <= 20000U) {
                    upd->battery_voltage_valid = true;
                    upd->battery_voltage_mv = raw * 10U;
                    ESP_LOGI(TAG, "candidate battery.voltage from report 0x0C raw=%" PRIu32 " => %" PRIu32 " mV",
                             raw, upd->battery_voltage_mv);
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
                if (raw >= 80U && raw <= 300U) {
                    upd->output_voltage_valid = true;
                    upd->output_voltage_mv = raw * 1000U;
                    ESP_LOGI(TAG, "candidate output.voltage from report 0x14 raw=%" PRIu32 " => %" PRIu32 " mV",
                             raw, upd->output_voltage_mv);
                }
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
                if (raw >= 80U && raw <= 300U) {
                    upd->input_voltage_valid = true;
                    upd->input_voltage_mv = raw * 1000U;
                    ESP_LOGI(TAG, "candidate input.voltage from report 0x21 raw=%" PRIu32 " => %" PRIu32 " mV",
                             raw, upd->input_voltage_mv);
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
