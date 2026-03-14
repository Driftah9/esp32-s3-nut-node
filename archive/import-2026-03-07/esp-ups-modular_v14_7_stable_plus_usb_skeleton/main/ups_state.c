#include "ups_state.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_timer.h"

static ups_state_t g_state;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void ups_state_init(ups_state_t *st) {
    if (st) memset(st, 0, sizeof(*st));
    portENTER_CRITICAL(&s_lock);
    memset(&g_state, 0, sizeof(g_state));
    portEXIT_CRITICAL(&s_lock);
}

void ups_state_set_demo_defaults(ups_state_t *st) {
    ups_state_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.battery_charge = 99;
    tmp.battery_runtime_s = 35640;
    tmp.input_utility_present = true;
    tmp.ups_flags = 0x0000000D;
    strcpy(tmp.ups_status, "OL");
    tmp.valid = false; // demo placeholders until USB populates real values
    tmp.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);

    portENTER_CRITICAL(&s_lock);
    g_state = tmp;
    portEXIT_CRITICAL(&s_lock);

    if (st) *st = tmp;
}

void ups_state_snapshot(ups_state_t *dst) {
    if (!dst) return;
    portENTER_CRITICAL(&s_lock);
    *dst = g_state;
    portEXIT_CRITICAL(&s_lock);
}
