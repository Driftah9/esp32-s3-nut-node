/*============================================================================
 ESP32-S3 UPS NUT Node
 CORE ORCHESTRATOR

 VERSION: v14.7-modular+usb-skel
 DATE: 2026-03-05

 REVERT HISTORY
 R0  v14.7 confirmed stable single-file baseline
 R1  v14.7 modular split (cfg_store, wifi_mgr, http_portal, nut_server)
 R2  v14.7 modular + USB skeleton (ups_state + ups_usb_hid)
============================================================================*/

#include "esp_log.h"
#include "nvs_flash.h"

#include "cfg_store.h"
#include "wifi_mgr.h"
#include "http_portal.h"
#include "nut_server.h"
#include "ups_state.h"
#include "ups_usb_hid.h"

static const char *TAG = "UPS_NODE";

static app_cfg_t g_cfg;
static ups_state_t g_ups;

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    cfg_store_load_or_defaults(&g_cfg);
    cfg_store_ensure_ap_ssid(&g_cfg);
    cfg_store_commit(&g_cfg);

    ups_state_init(&g_ups);
    ups_state_set_demo_defaults(&g_ups);

    wifi_mgr_start_apsta(&g_cfg);

    http_portal_start(&g_cfg);
    nut_server_start(&g_cfg);

    ups_usb_hid_start(&g_cfg);

    ESP_LOGI(TAG, "System started");
}
