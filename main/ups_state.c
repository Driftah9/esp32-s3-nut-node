/*============================================================================
 MODULE: ups_state

 REVERT HISTORY
 R0  v14.7  modular + USB skeleton
 R1  v14.8  NUT source-of-truth state pipe
 R2  v14.9  expanded metrics + identity support
 R3  v14.10 no API change, candidate metric support via update path
 R4  v14.16 adds ups_state_on_usb_disconnect()
 R5  v14.21 adds s_model_hint — detect_model() from product string
 R6  v14.23 apply_update sets battery_runtime_valid; disconnect clears it
 R7  v14.24 extract_firmware(), per-model battery_charge_low, compound status
 R8  v15.0  Remove model hint entirely — usage-based parser no longer needs it.
            extract_firmware() retained (useful for NUT ups.firmware variable).
            battery_charge_low default = 20 for all devices.
 R9  v15.2  Add ups_state_get_vid_pid() — used by ups_hid_parser for
            descriptor cross-check (XCHK) debug logging.
 R10 v15.7  Remove input_voltage/output_voltage from state and update structs.
 R11 v15.8  Re-add input_voltage_mv/output_voltage_mv — Feature report only.
            ups_state_on_usb_disconnect() now also clears them.

============================================================================*/
#include "ups_state.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "ups_state";
static ups_state_t  g_state;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void strlcpy0(char *dst, const char *src, size_t dstsz)
{
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
}

/* Extract firmware string from product string.
 * Handles APC format: "Back-UPS XS 1500M FW:947.d10 .D USB FW:d10"
 * Also handles plain: "SX550G" — returns empty if no "FW:" token found.
 */
static void extract_firmware(const char *product, char *dst, size_t dstsz)
{
    if (!product || !dst || dstsz == 0) { if (dst) dst[0] = 0; return; }
    const char *p = strstr(product, "FW:");
    if (!p) { dst[0] = 0; return; }
    p += 3;
    size_t i = 0;
    while (*p && *p != ' ' && i < dstsz - 1) {
        dst[i++] = *p++;
    }
    dst[i] = 0;
}

void ups_state_init(ups_state_t *st)
{
    if (st) memset(st, 0, sizeof(*st));
    portENTER_CRITICAL(&s_lock);
    memset(&g_state, 0, sizeof(g_state));
    portEXIT_CRITICAL(&s_lock);
}

void ups_state_set_demo_defaults(ups_state_t *st)
{
    ups_state_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.battery_charge        = 99;
    tmp.battery_runtime_s     = 35640;
    tmp.battery_runtime_valid = true;
    tmp.battery_charge_low    = 20;
    tmp.input_utility_present = true;
    tmp.ups_flags             = 0x00000001u;  /* charging bit */
    strlcpy(tmp.ups_status,   "OL", sizeof(tmp.ups_status));
    strlcpy(tmp.ups_firmware, "unknown", sizeof(tmp.ups_firmware));
    tmp.valid          = false;
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
    g_state.ups_load_valid         = false;
    g_state.battery_runtime_valid  = false;
    g_state.input_voltage_valid    = false;
    g_state.output_voltage_valid   = false;

    g_state.battery_charge         = 0;
    g_state.battery_runtime_s      = 0;
    g_state.input_utility_present  = false;
    g_state.ups_flags              = 0;
    g_state.battery_voltage_mv     = 0;
    g_state.ups_load_pct           = 0;
    g_state.input_voltage_mv       = 0;
    g_state.output_voltage_mv      = 0;

    strlcpy0(g_state.ups_status, "WAIT", sizeof(g_state.ups_status));

    /* Preserve: manufacturer, product, serial, ups_firmware, vid, pid,
     * battery_charge_low — all set at enumeration, survive disconnect. */

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

    if (upd->battery_charge_valid) g_state.battery_charge = upd->battery_charge;
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
    if (upd->ups_load_valid) {
        g_state.ups_load_pct    = upd->ups_load_pct;
        g_state.ups_load_valid  = true;
    }
    if (upd->input_voltage_valid) {
        g_state.input_voltage_mv    = upd->input_voltage_mv;
        g_state.input_voltage_valid = true;
    }
    if (upd->output_voltage_valid) {
        g_state.output_voltage_mv    = upd->output_voltage_mv;
        g_state.output_voltage_valid = true;
    }

    if (upd->ups_status[0]) {
        strlcpy0(g_state.ups_status, upd->ups_status, sizeof(g_state.ups_status));
    }

    g_state.valid = upd->valid || g_state.valid;
    g_state.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);

    portEXIT_CRITICAL(&s_lock);
}

void ups_state_set_usb_identity(uint16_t vid, uint16_t pid, uint16_t hid_report_desc_len,
                                const char *manufacturer, const char *product,
                                const char *serial)
{
    char fw[32] = {0};
    extract_firmware(product, fw, sizeof(fw));

    portENTER_CRITICAL(&s_lock);
    g_state.vid                = vid;
    g_state.pid                = pid;
    g_state.hid_report_desc_len = hid_report_desc_len;
    strlcpy0(g_state.manufacturer, manufacturer ? manufacturer : "UNKNOWN", sizeof(g_state.manufacturer));
    strlcpy0(g_state.product,      product      ? product      : "UNKNOWN", sizeof(g_state.product));
    strlcpy0(g_state.serial,       serial        ? serial       : "UNKNOWN", sizeof(g_state.serial));
    strlcpy0(g_state.ups_firmware, fw[0] ? fw : "unknown", sizeof(g_state.ups_firmware));
    g_state.battery_charge_low = 20;  /* safe default for all HID UPS devices */
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "USB identity: VID=%04X PID=%04X mfr='%s' product='%s' serial='%s' fw='%s'",
             (unsigned)vid, (unsigned)pid,
             manufacturer ? manufacturer : "",
             product      ? product      : "",
             serial       ? serial       : "",
             fw[0] ? fw : "unknown");
}

void ups_state_get_vid_pid(uint16_t *vid_out, uint16_t *pid_out)
{
    portENTER_CRITICAL(&s_lock);
    if (vid_out) *vid_out = g_state.vid;
    if (pid_out) *pid_out = g_state.pid;
    portEXIT_CRITICAL(&s_lock);
}
