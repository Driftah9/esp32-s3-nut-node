/*============================================================================
 MODULE: ups_hid_descriptor_map

 RESPONSIBILITY
 - Walk a HID report descriptor and log major items
 - Track usage page / usage / report id / size / count
 - Highlight Power Device / Battery System usage pages used by UPS HID devices

 NOTES
 - This is intentionally a logger-first artifact, not a full semantic mapper yet
 - The goal is to identify whether objects like UPS.Output.Voltage exist in the
   descriptor and what report/field structure they belong to

 REFERENCES
 - HID item format: short items with size/type/tag
 - USB HID Usage Tables:
   * Power Device page = 0x84
   * Battery System page = 0x85

============================================================================*/

#include "ups_hid_descriptor_map.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "hid_desc_map";

typedef struct {
    uint32_t usage_page;
    uint32_t report_size;
    uint32_t report_count;
    uint32_t report_id;
    int32_t logical_min;
    int32_t logical_max;
    int32_t physical_min;
    int32_t physical_max;
    uint32_t unit;
    uint32_t unit_exp;
    uint32_t usages[16];
    size_t usage_count;
    uint32_t usage_min;
    uint32_t usage_max;
    uint32_t collection_depth;
} hid_parse_state_t;

static uint32_t rd_u(const uint8_t *p, size_t n)
{
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        v |= ((uint32_t)p[i]) << (8U * i);
    }
    return v;
}

static int32_t rd_s(const uint8_t *p, size_t n)
{
    uint32_t u = rd_u(p, n);
    if (n == 1) return (int32_t)(int8_t)u;
    if (n == 2) return (int32_t)(int16_t)u;
    return (int32_t)u;
}

static const char *main_tag_name(uint8_t tag)
{
    switch (tag) {
        case 8:  return "Input";
        case 9:  return "Output";
        case 10: return "Collection";
        case 11: return "Feature";
        case 12: return "End Collection";
        default: return "Main?";
    }
}

static const char *global_tag_name(uint8_t tag)
{
    switch (tag) {
        case 0: return "Usage Page";
        case 1: return "Logical Min";
        case 2: return "Logical Max";
        case 3: return "Physical Min";
        case 4: return "Physical Max";
        case 5: return "Unit Exponent";
        case 6: return "Unit";
        case 7: return "Report Size";
        case 8: return "Report ID";
        case 9: return "Report Count";
        case 10: return "Push";
        case 11: return "Pop";
        default: return "Global?";
    }
}

static const char *local_tag_name(uint8_t tag)
{
    switch (tag) {
        case 0: return "Usage";
        case 1: return "Usage Min";
        case 2: return "Usage Max";
        default: return "Local?";
    }
}

static const char *usage_page_name(uint32_t page)
{
    switch (page) {
        case 0x01: return "Generic Desktop";
        case 0x07: return "Keyboard";
        case 0x08: return "LED";
        case 0x09: return "Button";
        case 0x84: return "Power Device";
        case 0x85: return "Battery System";
        default:   return "Other";
    }
}

static const char *power_device_usage_name(uint32_t u)
{
    switch (u) {
        case 0x04: return "UPS";
        case 0x1A: return "Input";
        case 0x1C: return "Output";
        case 0x24: return "Voltage";
        case 0x30: return "Voltage Config";
        case 0x40: return "Config Voltage";
        case 0x42: return "Present";
        case 0x44: return "Used";
        case 0x60: return "Percent Load";
        case 0x66: return "Remaining Capacity";
        case 0x68: return "Run Time To Empty";
        case 0xD0: return "AC Present";
        default:   return "PowerUsage";
    }
}

static const char *battery_usage_name(uint32_t u)
{
    switch (u) {
        case 0x28: return "Voltage";
        case 0x66: return "Remaining Capacity";
        case 0x68: return "Run Time To Empty";
        default:   return "BatteryUsage";
    }
}

static void reset_local(hid_parse_state_t *st)
{
    st->usage_count = 0;
    st->usage_min = 0;
    st->usage_max = 0;
}

static void push_usage(hid_parse_state_t *st, uint32_t usage)
{
    if (st->usage_count < (sizeof(st->usages) / sizeof(st->usages[0]))) {
        st->usages[st->usage_count++] = usage;
    }
}

static void log_usage(uint32_t page, uint32_t usage)
{
    if (page == 0x84) {
        ESP_LOGI(TAG, "      usage page 0x%02" PRIX32 " (%s), usage 0x%02" PRIX32 " (%s)",
                 page, usage_page_name(page), usage, power_device_usage_name(usage));
    } else if (page == 0x85) {
        ESP_LOGI(TAG, "      usage page 0x%02" PRIX32 " (%s), usage 0x%02" PRIX32 " (%s)",
                 page, usage_page_name(page), usage, battery_usage_name(usage));
    } else {
        ESP_LOGI(TAG, "      usage page 0x%02" PRIX32 " (%s), usage 0x%02" PRIX32,
                 page, usage_page_name(page), usage);
    }
}

static void log_field_context(const char *kind, uint32_t flags, const hid_parse_state_t *st)
{
    ESP_LOGI(TAG,
             "  %s: rpt_id=%" PRIu32 " size=%" PRIu32 " count=%" PRIu32
             " logical=[%" PRId32 ",%" PRId32 "] physical=[%" PRId32 ",%" PRId32 "]"
             " unit=0x%08" PRIX32 " exp=0x%08" PRIX32 " flags=0x%02" PRIX32,
             kind,
             st->report_id,
             st->report_size,
             st->report_count,
             st->logical_min,
             st->logical_max,
             st->physical_min,
             st->physical_max,
             st->unit,
             st->unit_exp,
             flags);

    if (st->usage_count > 0) {
        for (size_t i = 0; i < st->usage_count; i++) {
            log_usage(st->usage_page, st->usages[i]);
        }
    } else if (st->usage_min || st->usage_max) {
        ESP_LOGI(TAG,
                 "      usage range: page 0x%02" PRIX32 " (%s), 0x%02" PRIX32 " .. 0x%02" PRIX32,
                 st->usage_page, usage_page_name(st->usage_page), st->usage_min, st->usage_max);
    } else {
        ESP_LOGI(TAG, "      no explicit local usage attached");
    }
}

void ups_hid_descriptor_map_log(const uint8_t *desc, size_t len)
{
    if (!desc || len == 0) {
        ESP_LOGW(TAG, "No HID descriptor bytes provided");
        return;
    }

    hid_parse_state_t st;
    memset(&st, 0, sizeof(st));

    ESP_LOGI(TAG, "==== HID report descriptor walk start (len=%u) ====", (unsigned)len);

    for (size_t i = 0; i < len; ) {
        uint8_t b = desc[i++];

        if (b == 0xFE) {
            if (i + 2 > len) {
                ESP_LOGW(TAG, "Truncated long item");
                break;
            }
            uint8_t data_sz = desc[i++];
            uint8_t long_tag = desc[i++];
            ESP_LOGI(TAG, "LONG item tag=0x%02X size=%u", long_tag, data_sz);
            i += data_sz;
            if (i > len) {
                ESP_LOGW(TAG, "Descriptor ended inside long item");
                break;
            }
            continue;
        }

        uint8_t sz_code = (b & 0x03);
        uint8_t type = (b >> 2) & 0x03;
        uint8_t tag  = (b >> 4) & 0x0F;
        size_t data_sz = (sz_code == 3) ? 4U : (size_t)sz_code;

        if (i + data_sz > len) {
            ESP_LOGW(TAG, "Truncated short item tag=%u type=%u", tag, type);
            break;
        }

        const uint8_t *p = &desc[i];
        uint32_t u = rd_u(p, data_sz);
        int32_t s = rd_s(p, data_sz);

        switch (type) {
            case 0: {
                if (tag == 8 || tag == 9 || tag == 11) {
                    log_field_context(main_tag_name(tag), u, &st);
                    reset_local(&st);
                } else if (tag == 10) {
                    ESP_LOGI(TAG, "  Collection depth=%" PRIu32 " type=0x%02" PRIX32,
                             st.collection_depth, u);
                    if (st.usage_count > 0) {
                        for (size_t k = 0; k < st.usage_count; k++) {
                            log_usage(st.usage_page, st.usages[k]);
                        }
                    }
                    st.collection_depth++;
                    reset_local(&st);
                } else if (tag == 12) {
                    if (st.collection_depth > 0) st.collection_depth--;
                    ESP_LOGI(TAG, "  End Collection -> depth=%" PRIu32, st.collection_depth);
                    reset_local(&st);
                } else {
                    ESP_LOGI(TAG, "  %s raw=0x%02" PRIX32, main_tag_name(tag), u);
                    reset_local(&st);
                }
                break;
            }

            case 1: {
                switch (tag) {
                    case 0: st.usage_page   = u; break;
                    case 1: st.logical_min  = s; break;
                    case 2: st.logical_max  = s; break;
                    case 3: st.physical_min = s; break;
                    case 4: st.physical_max = s; break;
                    case 5: st.unit_exp     = u; break;
                    case 6: st.unit         = u; break;
                    case 7: st.report_size  = u; break;
                    case 8: st.report_id    = u; break;
                    case 9: st.report_count = u; break;
                    default: break;
                }
                ESP_LOGI(TAG, "  %s = 0x%0*" PRIX32 " (%" PRId32 ")",
                         global_tag_name(tag),
                         (int)(data_sz * 2U),
                         u, s);
                break;
            }

            case 2: {
                switch (tag) {
                    case 0:
                        push_usage(&st, u);
                        ESP_LOGI(TAG, "  %s = 0x%0*" PRIX32, local_tag_name(tag), (int)(data_sz * 2U), u);
                        log_usage(st.usage_page, u);
                        break;
                    case 1:
                        st.usage_min = u;
                        ESP_LOGI(TAG, "  %s = 0x%0*" PRIX32, local_tag_name(tag), (int)(data_sz * 2U), u);
                        break;
                    case 2:
                        st.usage_max = u;
                        ESP_LOGI(TAG, "  %s = 0x%0*" PRIX32, local_tag_name(tag), (int)(data_sz * 2U), u);
                        break;
                    default:
                        ESP_LOGI(TAG, "  %s = 0x%0*" PRIX32, local_tag_name(tag), (int)(data_sz * 2U), u);
                        break;
                }
                break;
            }

            default:
                ESP_LOGI(TAG, "  Reserved type=%u tag=%u raw=0x%0*" PRIX32,
                         type, tag, (int)(data_sz * 2U), u);
                break;
        }

        i += data_sz;
    }

    ESP_LOGI(TAG, "==== HID report descriptor walk end ====");
}
