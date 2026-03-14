/*============================================================================
 ESP32-S3 UPS NUT Node
 CORE ORCHESTRATOR

 VERSION: v14.8-modular-full-package
 DATE: 2026-03-05
 ESP-IDF: v5.3.1

 REVERT HISTORY
 R0  v14.7 confirmed-stable single-file baseline
 R1  v14.7 modular baseline (real WiFi/HTTP/NUT modules)
 R2  v14.7 modular + ups_state + USB skeleton
 R3  v14.8 modular + USB scan/identify module (this package)

============================================================================*/

// main.c - ESP32-S3 NUT Node (v14.7 CONFIRMED STABLE, modularized)
// ============================================================================
// VERSION: v14.7-stable-modular (2026-03-05)
// ESP-IDF target: v5.3.1
//
// This refactor keeps behavior identical to the confirmed-stable v14.7 single-file drop-in,
// but splits responsibilities into modules so core orchestration stays small.
//
// Modules:
//   - cfg_store.*   : NVS config load/save + defaults
//   - wifi_mgr.*    : SoftAP+STA bring-up + STA IP helper + event bits
//   - http_portal.* : raw-socket HTTP server + /config /save /status /reboot
//   - nut_server.*  : minimal NUT/upsd-like TCP server on 3493 (plaintext)
//
// ============================================================================

#include "esp_log.h"
#include "nvs_flash.h"

#include "cfg_store.h"
#include "wifi_mgr.h"
#include "http_portal.h"
#include "nut_server.h"
#include "ups_state.h"
#include "ups_usb_hid.h"

static const char *TAG = "UPS_USB_M14";

static app_cfg_t g_cfg;
static ups_state_t g_ups;

void app_main(void) {
    ESP_LOGI(TAG, "=======================================");
    ESP_LOGI(TAG, "ESP32 UPS NUT Node - v14.7 CONFIRMED STABLE (modular)");
    ESP_LOGI(TAG, "ESP-IDF v5.3.1 target");
    ESP_LOGI(TAG, "=======================================");

    ESP_ERROR_CHECK(nvs_flash_init());

    cfg_store_load_or_defaults(&g_cfg);
    cfg_store_ensure_ap_ssid(&g_cfg);
    cfg_store_commit(&g_cfg);

    // UPS state model (populated by USB/HID module)
    ups_state_init(&g_ups);
    ups_state_set_demo_defaults(&g_ups);
 // best-effort persist if defaults were generated

    wifi_mgr_start_apsta(&g_cfg);

    // USB UPS (HID) background task(s)
    ups_usb_hid_start(&g_cfg);


    http_portal_start(&g_cfg);
    nut_server_start(&g_cfg);

    ESP_LOGI(TAG, "Ready. SoftAP portal: http://%s/config", WIFI_MGR_SOFTAP_IP_STR);
}
