/*============================================================================
 MODULE: ups_hid_desc

 PURPOSE
 Parse USB HID Report Descriptor bytes into a flat field table.
 Replaces all APC-hardcoded logic.  Works for any HID Power Device.

 REVERT HISTORY
 R0  v15.0  Initial
 R1  v15.2  Added verbose debug logging at ESP_LOG_DEBUG level.
            Every item parsed, every field skipped (with reason), every
            usage expansion, and a final per-report-ID summary are logged
            so we can see exactly what the CyberPower descriptor contains
            and why fields are missing.

            To enable debug output, add to sdkconfig.defaults (or menuconfig):
              CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
            Or set per-module in sdkconfig:
              CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
            Or pass at build time:
              idf.py -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG build

            The debug lines are prefixed:
              D ups_hid_desc: [ITEM] ...  — every item as parsed
              D ups_hid_desc: [COLL] ...  — Collection open/close
              D ups_hid_desc: [MAIN] ...  — Main item (Input/Output/Feature)
              D ups_hid_desc: [SKIP] ...  — field skipped and WHY
              D ups_hid_desc: [KEPT] ...  — field accepted into table
              D ups_hid_desc: [RPT]  ...  — per-report-ID summary at end

 IMPLEMENTATION NOTES

 The HID report descriptor is a stream of tagged items.  Each item has:
   bits[1:0]  = bSize  (0=0 bytes, 1=1 byte, 2=2 bytes, 3=4 bytes)
   bits[3:2]  = bType  (0=Main, 1=Global, 2=Local, 3=Long)
   bits[7:4]  = bTag

 Main items:
   0x80 = Input      0x90 = Output      0xB0 = Feature
   0xA0 = Collection  0xC0 = End Collection

 Global items (persistent state, pushed/popped with 0xA4/0xB4):
   0x04 = Usage Page
   0x14 = Logical Minimum
   0x24 = Logical Maximum
   0x44 = Physical Minimum
   0x54 = Physical Maximum
   0x64 = Unit Exponent
   0x74 = Unit
   0x84 = Report Size (bits)
   0x94 = Report Count
   0xA4 = Push
   0xB4 = Pop
   0x84 = Report ID  -- NOTE: tag 0x08 = Report ID when combined: 0x85

 Local items (consumed on next Main item):
   0x08 = Usage
   0x18 = Usage Minimum
   0x28 = Usage Maximum
   0x38 = Designator Index

 We maintain a simple parse state machine and accumulate fields.
 We track the current context (Input vs Output vs Feature via the Main
 item that closes a group) and the bit offset within each report ID.

============================================================================*/

#include "ups_hid_desc.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"

static const char *TAG = "ups_hid_desc";

/* -------------------------------------------------------------------------
 HID item tags (top 4 bits of the prefix byte, shifted)
 Type 0 = Main, 1 = Global, 2 = Local
------------------------------------------------------------------------- */
#define HID_ITEM_TYPE_MAIN    0
#define HID_ITEM_TYPE_GLOBAL  1
#define HID_ITEM_TYPE_LOCAL   2

#define HID_TAG_INPUT           0x8u   /* Main */
#define HID_TAG_OUTPUT          0x9u   /* Main */
#define HID_TAG_FEATURE         0xBu   /* Main */
#define HID_TAG_COLLECTION      0xAu   /* Main */
#define HID_TAG_END_COLLECTION  0xCu   /* Main */

#define HID_TAG_USAGE_PAGE      0x0u   /* Global */
#define HID_TAG_LOGICAL_MIN     0x1u   /* Global */
#define HID_TAG_LOGICAL_MAX     0x2u   /* Global */
#define HID_TAG_PHYSICAL_MIN    0x3u   /* Global */
#define HID_TAG_PHYSICAL_MAX    0x4u   /* Global */
#define HID_TAG_UNIT_EXPONENT   0x5u   /* Global */
#define HID_TAG_UNIT            0x6u   /* Global */
#define HID_TAG_REPORT_SIZE     0x7u   /* Global */
#define HID_TAG_REPORT_ID       0x8u   /* Global */
#define HID_TAG_REPORT_COUNT    0x9u   /* Global */
#define HID_TAG_PUSH            0xAu   /* Global */
#define HID_TAG_POP             0xBu   /* Global */

#define HID_TAG_USAGE           0x0u   /* Local */
#define HID_TAG_USAGE_MIN       0x1u   /* Local */
#define HID_TAG_USAGE_MAX       0x2u   /* Local */

/* Maximum depth for push/pop global state stack */
#define HID_STACK_DEPTH 8

/* Maximum usage queue per report item (handles Usage arrays) */
#define MAX_USAGE_QUEUE 32

typedef struct {
    uint32_t usage_page;      /* full 32-bit page (may be 0xFF84 etc.) */
    int32_t  logical_min;
    int32_t  logical_max;
    int32_t  physical_min;
    int32_t  physical_max;
    int8_t   unit_exponent;
    uint32_t unit;
    uint8_t  report_size;     /* bits per field */
    uint8_t  report_count;    /* number of fields in this Main item */
    uint8_t  report_id;
} hid_global_t;

typedef struct {
    uint32_t usages[MAX_USAGE_QUEUE];
    uint8_t  usage_count;
    uint32_t usage_min;
    uint32_t usage_max;
    bool     has_usage_range;
} hid_local_t;

/* Track bit offsets per report ID per type */
typedef struct {
    uint8_t  report_id;
    uint16_t input_bits;
    uint16_t output_bits;
    uint16_t feature_bits;
    uint8_t  input_field_count;    /* debug: how many fields accepted for Input */
    uint8_t  skipped_count;        /* debug: how many fields skipped for this rid */
} report_bit_state_t;

/* -------------------------------------------------------------------------
 Helper: human-readable type string for debug
------------------------------------------------------------------------- */
static const char *rtype_str(uint8_t t) {
    return (t == 0) ? "Input" : (t == 1) ? "Output" : "Feature";
}

/* -------------------------------------------------------------------------
 Helper: read a signed integer from up to 4 bytes (little-endian, sign-ext)
------------------------------------------------------------------------- */
static int32_t read_item_value(const uint8_t *p, uint8_t size, bool sign_extend)
{
    if (size == 0) return 0;
    uint32_t raw = 0;
    for (uint8_t i = 0; i < size && i < 4; i++) {
        raw |= ((uint32_t)p[i] << (8u * i));
    }
    if (sign_extend && size < 4) {
        uint32_t sign_bit = 1u << (8u * size - 1u);
        if (raw & sign_bit) {
            raw |= ~(sign_bit - 1u) | ~(sign_bit);
        }
    }
    return (int32_t)raw;
}

/* Decode HID unit_exponent nibble → signed int8 */
static int8_t decode_unit_exponent(int32_t raw_val)
{
    /* HID spec: nibble 0x0–0x7 = +0 to +7, 0x8–0xF = -8 to -1 */
    uint8_t nibble = (uint8_t)(raw_val & 0x0F);
    if (nibble >= 8) {
        return (int8_t)((int8_t)nibble - 16);
    }
    return (int8_t)nibble;
}

/* Find or create report bit-state entry */
static report_bit_state_t *find_or_create_report(report_bit_state_t *rpt_states,
                                                   uint8_t *rpt_count,
                                                   uint8_t report_id)
{
    for (uint8_t i = 0; i < *rpt_count; i++) {
        if (rpt_states[i].report_id == report_id) return &rpt_states[i];
    }
    if (*rpt_count >= UPS_HID_MAX_REPORTS) return NULL;
    report_bit_state_t *e = &rpt_states[*rpt_count];
    (*rpt_count)++;
    memset(e, 0, sizeof(*e));
    e->report_id = report_id;
    return e;
}

/* -------------------------------------------------------------------------
 Public API
------------------------------------------------------------------------- */

void ups_hid_desc_init(hid_desc_t *desc)
{
    if (desc) memset(desc, 0, sizeof(*desc));
}

bool ups_hid_desc_parse(const uint8_t *desc_bytes, size_t desc_len, hid_desc_t *out)
{
    if (!desc_bytes || !desc_len || !out) return false;

    ESP_LOGD(TAG, "[PARSE] Starting — descriptor length=%u bytes", (unsigned)desc_len);

    hid_global_t global_state;
    memset(&global_state, 0, sizeof(global_state));

    hid_global_t stack[HID_STACK_DEPTH];
    uint8_t      stack_depth = 0;

    hid_local_t  local;
    memset(&local, 0, sizeof(local));

    report_bit_state_t rpt_bits[UPS_HID_MAX_REPORTS];
    uint8_t            rpt_count = 0;
    memset(rpt_bits, 0, sizeof(rpt_bits));

    bool     has_report_ids = false;
    uint16_t field_count    = 0;
    uint32_t total_skipped  = 0;   /* debug counter */
    uint8_t  coll_depth     = 0;   /* debug: collection nesting depth */

    size_t pos = 0;
    uint32_t item_num = 0;

    while (pos < desc_len) {
        const uint8_t prefix = desc_bytes[pos++];
        item_num++;

        /* Long item (bType=3) — skip */
        if ((prefix & 0x03u) == 3u) {
            uint8_t long_size = 0;
            if (pos < desc_len) long_size = desc_bytes[pos++];
            if (pos < desc_len) pos++; /* long tag byte */
            if (pos + long_size <= desc_len) pos += long_size;
            ESP_LOGD(TAG, "[ITEM #%"PRIu32" @%u] Long item skipped (size=%u)",
                     item_num, (unsigned)(pos - 1), long_size);
            continue;
        }

        const uint8_t bSize = prefix & 0x03u;
        const uint8_t bType = (prefix >> 2u) & 0x03u;
        const uint8_t bTag  = (prefix >> 4u) & 0x0Fu;

        uint8_t data_size = (bSize == 3u) ? 4u : bSize;
        if (pos + data_size > desc_len) {
            ESP_LOGW(TAG, "[ITEM #%"PRIu32" @%u] Truncated item at end of descriptor — stopping",
                     item_num, (unsigned)pos);
            break;
        }

        const uint8_t *data_ptr = &desc_bytes[pos];
        pos += data_size;

        int32_t val = read_item_value(data_ptr, data_size, false);

        switch (bType) {

        /* ---- GLOBAL ---- */
        case HID_ITEM_TYPE_GLOBAL:
            switch (bTag) {
            case HID_TAG_USAGE_PAGE:
                global_state.usage_page = (uint32_t)val;
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global UsagePage=0x%04"PRIx32,
                         item_num, global_state.usage_page);
                break;
            case HID_TAG_LOGICAL_MIN:
                global_state.logical_min = read_item_value(data_ptr, data_size, true);
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global LogMin=%"PRId32,
                         item_num, global_state.logical_min);
                break;
            case HID_TAG_LOGICAL_MAX:
                global_state.logical_max = read_item_value(data_ptr, data_size,
                    (global_state.logical_min < 0));
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global LogMax=%"PRId32,
                         item_num, global_state.logical_max);
                break;
            case HID_TAG_PHYSICAL_MIN:
                global_state.physical_min = read_item_value(data_ptr, data_size, true);
                break;
            case HID_TAG_PHYSICAL_MAX:
                global_state.physical_max = read_item_value(data_ptr, data_size, true);
                break;
            case HID_TAG_UNIT_EXPONENT:
                global_state.unit_exponent = decode_unit_exponent(val);
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global UnitExp raw=0x%02"PRIx32" decoded=%d",
                         item_num, (uint32_t)(val & 0x0F), (int)global_state.unit_exponent);
                break;
            case HID_TAG_UNIT:
                global_state.unit = (uint32_t)val;
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global Unit=0x%08"PRIx32,
                         item_num, global_state.unit);
                break;
            case HID_TAG_REPORT_SIZE:
                global_state.report_size = (uint8_t)val;
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global ReportSize=%u bits",
                         item_num, global_state.report_size);
                break;
            case HID_TAG_REPORT_ID:
                global_state.report_id = (uint8_t)val;
                has_report_ids = true;
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global ReportID=0x%02X",
                         item_num, global_state.report_id);
                break;
            case HID_TAG_REPORT_COUNT:
                global_state.report_count = (uint8_t)val;
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global ReportCount=%u",
                         item_num, global_state.report_count);
                break;
            case HID_TAG_PUSH:
                if (stack_depth < HID_STACK_DEPTH) {
                    stack[stack_depth++] = global_state;
                    ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global Push (depth now %u)",
                             item_num, stack_depth);
                } else {
                    ESP_LOGW(TAG, "[ITEM #%"PRIu32"] Global Push OVERFLOW (max depth %d)",
                             item_num, HID_STACK_DEPTH);
                }
                break;
            case HID_TAG_POP:
                if (stack_depth > 0) {
                    global_state = stack[--stack_depth];
                    ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global Pop (depth now %u)",
                             item_num, stack_depth);
                } else {
                    ESP_LOGW(TAG, "[ITEM #%"PRIu32"] Global Pop UNDERFLOW", item_num);
                }
                break;
            default:
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Global tag=0x%X val=%"PRId32" (unhandled)",
                         item_num, bTag, val);
                break;
            }
            break;

        /* ---- LOCAL ---- */
        case HID_ITEM_TYPE_LOCAL:
            switch (bTag) {
            case HID_TAG_USAGE:
                if (local.usage_count < MAX_USAGE_QUEUE) {
                    uint32_t uk;
                    if (data_size <= 2) {
                        uk = (global_state.usage_page << 16) | (uint32_t)(val & 0xFFFF);
                    } else {
                        uk = (uint32_t)val;
                    }
                    local.usages[local.usage_count++] = uk;
                    ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Local Usage[%u]=0x%08"PRIx32
                             " (page=0x%02X uid=0x%04X)",
                             item_num, local.usage_count - 1, uk,
                             (unsigned)(uk >> 16), (unsigned)(uk & 0xFFFF));
                } else {
                    ESP_LOGW(TAG, "[ITEM #%"PRIu32"] Local Usage QUEUE FULL (%d max) — dropped",
                             item_num, MAX_USAGE_QUEUE);
                }
                break;
            case HID_TAG_USAGE_MIN:
                local.usage_min = (data_size <= 2)
                    ? ((global_state.usage_page << 16) | (uint32_t)(val & 0xFFFF))
                    : (uint32_t)val;
                local.has_usage_range = true;
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Local UsageMin=0x%08"PRIx32,
                         item_num, local.usage_min);
                break;
            case HID_TAG_USAGE_MAX:
                local.usage_max = (data_size <= 2)
                    ? ((global_state.usage_page << 16) | (uint32_t)(val & 0xFFFF))
                    : (uint32_t)val;
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Local UsageMax=0x%08"PRIx32
                         " (range %u entries)",
                         item_num, local.usage_max,
                         (unsigned)(local.usage_max - local.usage_min + 1));
                break;
            default:
                ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Local tag=0x%X val=%"PRId32" (unhandled)",
                         item_num, bTag, val);
                break;
            }
            break;

        /* ---- MAIN ---- */
        case HID_ITEM_TYPE_MAIN:
            if (bTag == HID_TAG_COLLECTION) {
                coll_depth++;
                ESP_LOGD(TAG, "[COLL #%"PRIu32"] Collection open (type=0x%02"PRIx32
                         " depth=%u page=0x%04"PRIx32")",
                         item_num, (uint32_t)val, coll_depth,
                         global_state.usage_page);
                /* Reset local state — collections don't produce fields */
                memset(&local, 0, sizeof(local));
                break;
            }
            if (bTag == HID_TAG_END_COLLECTION) {
                if (coll_depth > 0) coll_depth--;
                ESP_LOGD(TAG, "[COLL #%"PRIu32"] End Collection (depth now %u)",
                         item_num, coll_depth);
                break;
            }
            if (bTag == HID_TAG_INPUT ||
                bTag == HID_TAG_OUTPUT ||
                bTag == HID_TAG_FEATURE) {

                uint8_t report_type = (bTag == HID_TAG_INPUT)   ? 0u :
                                      (bTag == HID_TAG_OUTPUT)  ? 1u : 2u;

                /* Expand usage range into usage queue if needed */
                if (local.has_usage_range) {
                    uint32_t umin = local.usage_min;
                    uint32_t umax = local.usage_max;
                    uint32_t range_len = umax - umin + 1;
                    uint8_t  slots_free = MAX_USAGE_QUEUE - local.usage_count;
                    uint32_t will_add   = (range_len < slots_free) ? range_len : slots_free;

                    ESP_LOGD(TAG, "[MAIN #%"PRIu32"] UsageRange expand: 0x%08"PRIx32
                             "..0x%08"PRIx32" (%"PRIu32" entries, adding %"PRIu32")",
                             item_num, umin, umax, range_len, will_add);

                    if (range_len > slots_free) {
                        ESP_LOGW(TAG, "[MAIN #%"PRIu32"] UsageRange TRUNCATED: "
                                 "need %"PRIu32" slots but only %u free in queue",
                                 item_num, range_len, slots_free);
                    }

                    for (uint32_t u = umin;
                         u <= umax && local.usage_count < MAX_USAGE_QUEUE;
                         u++) {
                        local.usages[local.usage_count++] = u;
                    }
                    local.has_usage_range = false;
                }

                ESP_LOGD(TAG, "[MAIN #%"PRIu32"] %s rid=0x%02X count=%u size=%ubits "
                         "page=0x%04"PRIx32" usages_queued=%u logmin=%"PRId32" logmax=%"PRId32
                         " exp=%d coll_depth=%u",
                         item_num, rtype_str(report_type),
                         global_state.report_id,
                         global_state.report_count,
                         global_state.report_size,
                         global_state.usage_page,
                         local.usage_count,
                         global_state.logical_min,
                         global_state.logical_max,
                         (int)global_state.unit_exponent,
                         coll_depth);

                /* Each field occupies report_size bits, there are report_count of them */
                report_bit_state_t *rpt = find_or_create_report(rpt_bits, &rpt_count,
                                                                  global_state.report_id);
                if (!rpt) {
                    ESP_LOGE(TAG, "[MAIN #%"PRIu32"] report table full — skipping entire item",
                             item_num);
                    memset(&local, 0, sizeof(local));
                    break;
                }

                for (uint8_t i = 0; i < global_state.report_count; i++) {
                    uint16_t *bit_ptr = (report_type == 0) ? &rpt->input_bits :
                                        (report_type == 1) ? &rpt->output_bits :
                                                             &rpt->feature_bits;
                    uint16_t current_bit = *bit_ptr;
                    *bit_ptr = (uint16_t)(*bit_ptr + global_state.report_size);

                    /* Determine usage for this field index */
                    uint32_t usage_key = 0;
                    if (i < local.usage_count) {
                        usage_key = local.usages[i];
                    } else if (local.usage_count > 0) {
                        /* Reuse last usage (constant/padding fields often do this) */
                        usage_key = local.usages[local.usage_count - 1];
                        ESP_LOGD(TAG, "[MAIN #%"PRIu32"] field[%u]: reusing last usage "
                                 "0x%08"PRIx32" (count=%u < report_count=%u)",
                                 item_num, i, usage_key,
                                 local.usage_count, global_state.report_count);
                    }

                    /* Extract page and usage ID (handle vendor pages like 0xFF84) */
                    uint8_t  page    = (uint8_t)((usage_key >> 16) & 0xFFu);
                    uint16_t uid     = (uint16_t)(usage_key & 0xFFFFu);

                    /* Normalise vendor pages to standard (0xFF84→0x84, 0xFF85→0x85) */
                    uint32_t hi = (usage_key >> 16) & 0xFF00u;
                    if (hi == 0xFF00u) {
                        page = (uint8_t)(usage_key >> 16);
                    }

                    /* Only record fields on pages we care about:
                     * 0x84, 0x85, 0xFF84, 0xFF85 */
                    bool interesting =
                        (page == HID_PAGE_POWER_DEVICE)   ||
                        (page == HID_PAGE_BATTERY_SYSTEM) ||
                        ((usage_key >> 16) == 0xFF84u)     ||
                        ((usage_key >> 16) == 0xFF85u);

                    if (global_state.report_size == 0) {
                        ESP_LOGD(TAG, "[SKIP #%"PRIu32"] field[%u] rid=0x%02X "
                                 "usage=0x%08"PRIx32": zero bit_size — padding/constant",
                                 item_num, i, global_state.report_id, usage_key);
                        rpt->skipped_count++;
                        total_skipped++;
                        continue;
                    }

                    if (!interesting) {
                        ESP_LOGD(TAG, "[SKIP #%"PRIu32"] field[%u] rid=0x%02X "
                                 "usage=0x%08"PRIx32" (page=0x%04X): NOT power/battery page",
                                 item_num, i, global_state.report_id,
                                 usage_key, (unsigned)(usage_key >> 16));
                        rpt->skipped_count++;
                        total_skipped++;
                        continue;
                    }

                    /* Normalise 0xFF84/0xFF85 to 0x84/0x85 */
                    if ((usage_key >> 16) == 0xFF84u) page = HID_PAGE_POWER_DEVICE;
                    if ((usage_key >> 16) == 0xFF85u) page = HID_PAGE_BATTERY_SYSTEM;

                    if (field_count >= UPS_HID_MAX_FIELDS) {
                        ESP_LOGW(TAG, "[SKIP #%"PRIu32"] field[%u] rid=0x%02X: "
                                 "field table FULL (%d max)",
                                 item_num, i, global_state.report_id, UPS_HID_MAX_FIELDS);
                        total_skipped++;
                        goto done;
                    }

                    hid_field_t *f = &out->fields[field_count++];
                    f->report_id     = global_state.report_id;
                    f->report_type   = report_type;
                    f->bit_offset    = current_bit;
                    f->bit_size      = global_state.report_size;
                    f->usage_page    = page;
                    f->usage_id      = uid;
                    f->logical_min   = global_state.logical_min;
                    f->logical_max   = global_state.logical_max;
                    f->unit_exponent = global_state.unit_exponent;
                    f->is_signed     = (global_state.logical_min < 0);

                    if (report_type == 0) rpt->input_field_count++;

                    ESP_LOGD(TAG, "[KEPT #%"PRIu32"] field[%u] -> table[%u] "
                             "%s rid=0x%02X bit=%u sz=%u page=0x%02X uid=0x%04X "
                             "lmin=%"PRId32" lmax=%"PRId32" exp=%d",
                             item_num, i, field_count - 1,
                             rtype_str(report_type),
                             global_state.report_id, current_bit,
                             global_state.report_size,
                             page, uid,
                             global_state.logical_min,
                             global_state.logical_max,
                             (int)global_state.unit_exponent);
                }

                /* Reset local state after consuming */
                memset(&local, 0, sizeof(local));
            } else {
                ESP_LOGD(TAG, "[MAIN #%"PRIu32"] Unknown Main tag=0x%X val=%"PRId32,
                         item_num, bTag, val);
            }
            break;

        default:
            ESP_LOGD(TAG, "[ITEM #%"PRIu32"] Unknown bType=%u tag=0x%X val=%"PRId32,
                     item_num, bType, bTag, val);
            break;
        } /* switch bType */
    } /* while pos < desc_len */

done:
    out->field_count    = field_count;
    out->has_report_ids = has_report_ids;
    out->valid          = (field_count > 0);

    /* Copy report sizes */
    uint8_t dst_rpt = 0;
    for (uint8_t i = 0; i < rpt_count && dst_rpt < UPS_HID_MAX_REPORTS; i++) {
        out->reports[dst_rpt].report_id     = rpt_bits[i].report_id;
        out->reports[dst_rpt].input_bytes   = (uint16_t)((rpt_bits[i].input_bits   + 7) / 8);
        out->reports[dst_rpt].output_bytes  = (uint16_t)((rpt_bits[i].output_bits  + 7) / 8);
        out->reports[dst_rpt].feature_bytes = (uint16_t)((rpt_bits[i].feature_bits + 7) / 8);
        dst_rpt++;
    }
    out->report_count = dst_rpt;

    /* Summary at INFO level — always visible */
    ESP_LOGI(TAG, "Parsed HID descriptor: %u fields, %u report IDs, report_ids=%s "
             "(items=%"PRIu32" skipped=%"PRIu32")",
             (unsigned)field_count, (unsigned)dst_rpt,
             has_report_ids ? "yes" : "no",
             item_num, total_skipped);

    /* Per-report-ID summary at DEBUG level */
    ESP_LOGD(TAG, "[RPT] Per-report-ID breakdown (%u IDs seen):", rpt_count);
    for (uint8_t i = 0; i < rpt_count; i++) {
        ESP_LOGD(TAG, "[RPT]   rid=0x%02X  in=%ubytes(%ufields_kept %uskipped) "
                 "out=%ubytes feat=%ubytes",
                 rpt_bits[i].report_id,
                 (unsigned)((rpt_bits[i].input_bits   + 7) / 8),
                 rpt_bits[i].input_field_count,
                 rpt_bits[i].skipped_count,
                 (unsigned)((rpt_bits[i].output_bits  + 7) / 8),
                 (unsigned)((rpt_bits[i].feature_bits + 7) / 8));
    }

    /* Cross-check: warn about report IDs seen in interrupt data but not in descriptor.
     * We log this at WARN so it appears even without debug enabled.
     * These rids were first seen on CyberPower devices but also appear identically
     * on Eaton 3S (PID FFFF) and potentially other vendors. The list is device-agnostic
     * — it flags any undeclared rid we know carries live UPS state on at least one device.
     * NOTE: This is a STATIC pre-seeded list built from prior submissions. The warning
     * fires at enumeration time based on what the descriptor does NOT declare; it does NOT
     * reflect what the currently connected device is actually sending at runtime.
     */
    static const uint8_t expected_rids[] = {
        0x20, 0x21, 0x22, 0x23, 0x25, 0x28, 0x29,
        0x80, 0x82, 0x85, 0x86, 0x87, 0x88
    };
    for (uint8_t ei = 0; ei < sizeof(expected_rids); ei++) {
        uint8_t rid = expected_rids[ei];
        bool found_in_desc = false;
        for (uint8_t di = 0; di < rpt_count; di++) {
            if (rpt_bits[di].report_id == rid) {
                found_in_desc = true;
                break;
            }
        }
        if (!found_in_desc) {
            ESP_LOGW(TAG, "[XCHK] rid=0x%02X seen in interrupt data but NOT in parsed descriptor!", rid);
        }
    }

    return out->valid;
}

const hid_field_t *ups_hid_desc_find_input(const hid_desc_t *desc,
                                             uint8_t usage_page,
                                             uint16_t usage_id)
{
    if (!desc || !desc->valid) return NULL;
    for (uint16_t i = 0; i < desc->field_count; i++) {
        const hid_field_t *f = &desc->fields[i];
        if (f->report_type == 0 &&
            f->usage_page  == usage_page &&
            f->usage_id    == usage_id) {
            return f;
        }
    }
    return NULL;
}

uint8_t ups_hid_desc_find_inputs_by_page(const hid_desc_t *desc,
                                          uint8_t usage_page,
                                          const hid_field_t **out_fields,
                                          uint8_t max_out)
{
    uint8_t found = 0;
    if (!desc || !desc->valid || !out_fields || max_out == 0) return 0;
    for (uint16_t i = 0; i < desc->field_count && found < max_out; i++) {
        const hid_field_t *f = &desc->fields[i];
        if (f->report_type == 0 && f->usage_page == usage_page) {
            out_fields[found++] = f;
        }
    }
    return found;
}

bool ups_hid_desc_extract_field(const uint8_t *data, size_t data_len,
                                 const hid_field_t *field,
                                 int32_t *out_raw)
{
    if (!data || !field || !out_raw) return false;
    if (field->bit_size == 0) return false;

    uint16_t start_bit = field->bit_offset;
    uint16_t num_bits  = field->bit_size;

    /* Check bounds */
    uint16_t last_bit = (uint16_t)(start_bit + num_bits - 1u);
    if ((last_bit / 8u) >= data_len) return false;

    /* Extract up to 32 bits */
    uint32_t raw = 0u;
    for (uint16_t b = 0; b < num_bits; b++) {
        uint16_t abs_bit  = (uint16_t)(start_bit + b);
        uint8_t  byte_idx = (uint8_t)(abs_bit / 8u);
        uint8_t  bit_idx  = (uint8_t)(abs_bit % 8u);
        if ((data[byte_idx] >> bit_idx) & 1u) {
            raw |= (1u << b);
        }
    }

    /* Sign extend if needed */
    if (field->is_signed && num_bits < 32u) {
        uint32_t sign_bit = 1u << (num_bits - 1u);
        if (raw & sign_bit) {
            raw |= ~(sign_bit - 1u) | ~(sign_bit);
        }
    }

    *out_raw = (int32_t)raw;
    return true;
}

bool ups_hid_desc_to_milli(int32_t raw, int8_t unit_exponent, int32_t *out_milli)
{
    if (!out_milli) return false;

    /* We want result in milli-units (mV, mA, ms, etc.)
     * result = raw * 10^(unit_exponent) * 10^3
     *        = raw * 10^(unit_exponent + 3)
     */
    int total_exp = (int)unit_exponent + 3;

    if (total_exp == 0) {
        *out_milli = raw;
        return true;
    }

    int64_t result = (int64_t)raw;
    if (total_exp > 0) {
        for (int i = 0; i < total_exp && i < 9; i++) result *= 10;
    } else {
        int neg = -total_exp;
        for (int i = 0; i < neg && i < 9; i++) result /= 10;
    }

    if (result > INT32_MAX || result < INT32_MIN) return false;
    *out_milli = (int32_t)result;
    return true;
}

void ups_hid_desc_dump(const hid_desc_t *desc)
{
    if (!desc || !desc->valid) {
        ESP_LOGI(TAG, "[DUMP] descriptor not valid");
        return;
    }
    ESP_LOGI(TAG, "[DUMP] %u fields, %u reports", desc->field_count, desc->report_count);
    for (uint16_t i = 0; i < desc->field_count; i++) {
        const hid_field_t *f = &desc->fields[i];
        ESP_LOGI(TAG, "  [%3u] rid=%02X type=%u bit_off=%3u size=%2u page=%02X uid=%04X lmin=%"PRId32" lmax=%"PRId32" exp=%d",
                 (unsigned)i,
                 (unsigned)f->report_id,
                 (unsigned)f->report_type,
                 (unsigned)f->bit_offset,
                 (unsigned)f->bit_size,
                 (unsigned)f->usage_page,
                 (unsigned)f->usage_id,
                 f->logical_min,
                 f->logical_max,
                 (int)f->unit_exponent);
    }
}
