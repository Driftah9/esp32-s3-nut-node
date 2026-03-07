/*============================================================================
 MODULE: ups_state

 REVERT HISTORY
 R0  v14.7 modular + USB skeleton
 R1  v14.8 retained for NUT source-of-truth state pipe
 R2  v14.9 expanded metrics + identity support
 R3  v14.10 no API change, candidate metric support via update path
 R4  v14.16 adds ups_state_on_usb_disconnect()
 R5  v14.21 adds s_model_hint — detect_model() from product string
 R6  v14.23 apply_update sets battery_runtime_valid; disconnect clears it
 R7  v14.24 extract_firmware() parses "FW:xxx" from product string into
            ups_firmware field. Per-model battery_charge_low thresholds.
            ups_status expanded to 16 bytes for compound NUT status tokens.

============================================================================*/
#include "ups_state.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "ups_state";
static ups_state_t g_state;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ups_model_hint_t s_model_hint = UPS_MODEL_UNKNOWN;

static void strlcpy0_local(char *dst, const char *src, size_t dstsz)
{
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
}

static ups_model_hint_t detect_model(const char *product)
{
    if (!product) return UPS_MODEL_UNKNOWN;
    if (strstr(product, "BR1000G"))  return UPS_MODEL_BR1000G;
    if (strstr(product, "XS 1500M")) return UPS_MODEL_XS1500M;
    return UPS_MODEL_UNKNOWN;
}

/* Extract firmware version from APC product string.
 * Product strings have the form:
 *   "Back-UPS XS 1500M FW:947.d10 .D USB FW:d10"
 *   "Back-UPS BR1000G FW:xxx.yy ..."
 * We capture the first token after "FW:" up to the next space. */
static void extract_firmware(const char *product, char *dst, size_t dstsz)
{
    if (!product || !dst || dstsz == 0) { if (dst) dst[0] = 0; return; }
    const char *p = strstr(product, "FW:");
    if (!p) { dst[0] = 0; return; }
    p += 3; /* skip "FW:" */
    size_t i = 0;
    while (*p && *p != ' ' && i < dstsz - 1) {
        dst[i++] = *p++;
    }
    dst[i] = 0;
}

/* Per-model low-battery threshold.
 * These match the values APC firmware typically reports and what
 * upsmon/HA use as shutdown trigger. Conservative defaults. */
static uint8_t charge_low_for_model(ups_model_hint_t hint)
{
    switch (hint) {
        case UPS_MODEL_BR1000G:  return 20;
        case UPS_MODEL_XS1500M:  return 20;
        default:                 return 20;  /* safe default for any APC Back-UPS */
    }
}

void ups_state_init(ups_state_t *st)
{
    if (st) memset(st, 0, sizeof(*st));
    portENTER_CRITICAL(&s_lock);
    memset(&g_state, 0, sizeof(g_state));
    s_model_hint = UPS_MODEL_UNKNOWN;
    portEXIT_CRITICAL(&s_lock);
}

void ups_state_set_demo_defaults(ups_state_t *st)
{
    ups_state_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.battery_charge = 99;
    tmp.battery_runtime_s = 35640;
    tmp.battery_runtime_valid = true;
    tmp.battery_charge_low = 20;
    tmp.input_utility_present = true;
    tmp.ups_flags = 0x0000000D;
    strlcpy(tmp.ups_status, "OL", sizeof(tmp.ups_status));
    strlcpy(tmp.ups_firmware, "unknown", sizeof(tmp.ups_firmware));
    tmp.valid = false;
    tmp.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);

    portENTER_CRITICAL(&s_lock);
    g_state = tmp;
    portEXIT_CRITICAL(&s_lock);

    if (st) *st = tmp;
}

void ups_state_on_usb_disconnect(void)
{
    portENTER_CRITICAL(&s_lock);

    g_state.valid                  = false;
    g_state.battery_voltage_valid  = false;
    g_state.input_voltage_valid    = false;
    g_state.output_voltage_valid   = false;
    g_state.ups_load_valid         = false;
    g_state.battery_runtime_valid  = false;

    g_state.battery_charge         = 0;
    g_state.battery_runtime_s      = 0;
    g_state.input_utility_present  = false;
    g_state.ups_flags              = 0;
    g_state.battery_voltage_mv     = 0;
    g_state.input_voltage_mv       = 0;
    g_state.output_voltage_mv      = 0;
    g_state.ups_load_pct           = 0;

    strlcpy0_local(g_state.ups_status, "WAIT", sizeof(g_state.ups_status));

    /* Preserve: manufacturer, product, serial, ups_firmware, vid, pid,
     *           battery_charge_low, model hint — all stay from enumeration. */

    g_state.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);

    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "ups_state invalidated on USB disconnect");
}

void ups_state_snapshot(ups_state_t *dst)
{
    if (!dst) return;
    portENTER_CRITICAL(&s_lock);
    *dst = g_state;
    portEXIT_CRITICAL(&s_lock);
}

void ups_state_apply_update(const ups_state_update_t *upd)
{
    if (!upd) return;

    portENTER_CRITICAL(&s_lock);

    if (upd->battery_charge_valid)        g_state.battery_charge = upd->battery_charge;
    if (upd->battery_runtime_valid) {
        g_state.battery_runtime_s     = upd->battery_runtime_s;
        g_state.battery_runtime_valid = true;
    }
    if (upd->input_utility_present_valid) g_state.input_utility_present = upd->input_utility_present;
    if (upd->ups_flags_valid)             g_state.ups_flags = upd->ups_flags;

    if (upd->battery_voltage_valid) {
        g_state.battery_voltage_mv    = upd->battery_voltage_mv;
        g_state.battery_voltage_valid = true;
    }
    if (upd->input_voltage_valid) {
        g_state.input_voltage_mv    = upd->input_voltage_mv;
        g_state.input_voltage_valid = true;
    }
    if (upd->output_voltage_valid) {
        g_state.output_voltage_mv    = upd->output_voltage_mv;
        g_state.output_voltage_valid = true;
    }
    if (upd->ups_load_valid) {
        g_state.ups_load_pct    = upd->ups_load_pct;
        g_state.ups_load_valid  = true;
    }

    if (upd->ups_status[0]) {
        strlcpy0_local(g_state.ups_status, upd->ups_status, sizeof(g_state.ups_status));
    }

    g_state.valid = upd->valid || g_state.valid;
    g_state.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);

    portEXIT_CRITICAL(&s_lock);
}

void ups_state_set_usb_identity(uint16_t vid, uint16_t pid, uint16_t hid_report_desc_len,
                                const char *manufacturer, const char *product, const char *serial)
{
    ups_model_hint_t hint = detect_model(product);
    char fw[32] = {0};
    extract_firmware(product, fw, sizeof(fw));
    uint8_t chg_low = charge_low_for_model(hint);

    portENTER_CRITICAL(&s_lock);
    g_state.vid = vid;
    g_state.pid = pid;
    g_state.hid_report_desc_len = hid_report_desc_len;
    strlcpy0_local(g_state.manufacturer, manufacturer, sizeof(g_state.manufacturer));
    strlcpy0_local(g_state.product, product, sizeof(g_state.product));
    strlcpy0_local(g_state.serial, serial, sizeof(g_state.serial));
    strlcpy0_local(g_state.ups_firmware, fw[0] ? fw : "unknown", sizeof(g_state.ups_firmware));
    g_state.battery_charge_low = chg_low;
    s_model_hint = hint;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "model hint: %s (%d)  fw='%s'  charge_low=%u%%",
             hint == UPS_MODEL_BR1000G  ? "BR1000G"  :
             hint == UPS_MODEL_XS1500M  ? "XS1500M"  : "UNKNOWN",
             (int)hint, fw[0] ? fw : "unknown", (unsigned)chg_low);
}

ups_model_hint_t ups_state_get_model_hint(void)
{
    ups_model_hint_t h;
    portENTER_CRITICAL(&s_lock);
    h = s_model_hint;
    portEXIT_CRITICAL(&s_lock);
    return h;
}
