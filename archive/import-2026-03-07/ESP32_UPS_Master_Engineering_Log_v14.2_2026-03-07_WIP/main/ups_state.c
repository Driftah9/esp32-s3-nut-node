/*============================================================================
 MODULE: ups_state

 REVERT HISTORY
 R0  v14.7 modular + USB skeleton
 R1  v14.8 retained for NUT source-of-truth state pipe
 R2  v14.9 expanded metrics + identity support
 R3  v14.10 no API change, candidate metric support via update path

============================================================================*/
#include "ups_state.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_timer.h"

static ups_state_t g_state;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void strlcpy0_local(char *dst, const char *src, size_t dstsz)
{
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
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
    tmp.battery_charge = 99;
    tmp.battery_runtime_s = 35640;
    tmp.input_utility_present = true;
    tmp.ups_flags = 0x0000000D;
    strcpy(tmp.ups_status, "OL");
    tmp.valid = false;
    tmp.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);

    portENTER_CRITICAL(&s_lock);
    g_state = tmp;
    portEXIT_CRITICAL(&s_lock);

    if (st) *st = tmp;
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
    if (upd->battery_runtime_valid)       g_state.battery_runtime_s = upd->battery_runtime_s;
    if (upd->input_utility_present_valid) g_state.input_utility_present = upd->input_utility_present;
    if (upd->ups_flags_valid)             g_state.ups_flags = upd->ups_flags;

    if (upd->battery_voltage_valid) {
        g_state.battery_voltage_mv = upd->battery_voltage_mv;
        g_state.battery_voltage_valid = true;
    }
    if (upd->input_voltage_valid) {
        g_state.input_voltage_mv = upd->input_voltage_mv;
        g_state.input_voltage_valid = true;
    }
    if (upd->output_voltage_valid) {
        g_state.output_voltage_mv = upd->output_voltage_mv;
        g_state.output_voltage_valid = true;
    }
    if (upd->ups_load_valid) {
        g_state.ups_load_pct = upd->ups_load_pct;
        g_state.ups_load_valid = true;
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
    portENTER_CRITICAL(&s_lock);
    g_state.vid = vid;
    g_state.pid = pid;
    g_state.hid_report_desc_len = hid_report_desc_len;
    strlcpy0_local(g_state.manufacturer, manufacturer, sizeof(g_state.manufacturer));
    strlcpy0_local(g_state.product, product, sizeof(g_state.product));
    strlcpy0_local(g_state.serial, serial, sizeof(g_state.serial));
    portEXIT_CRITICAL(&s_lock);
}
