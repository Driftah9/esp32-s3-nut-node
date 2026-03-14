#include "ups_usb_hid.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ups_usb_hid";

bool ups_usb_hid_is_enabled(void) {
#if defined(CONFIG_USB_HOST_ENABLED) && CONFIG_USB_HOST_ENABLED
    return true;
#else
    return false;
#endif
}

#if defined(CONFIG_USB_HOST_ENABLED) && CONFIG_USB_HOST_ENABLED

// NOTE:
// This is a *skeleton* module whose job is to:
//  1) bring up USB host
//  2) detect UPS VID:PID
//  3) claim HID interface and stream interrupt reports
//  4) decode into ups_state_t
//
// We keep this minimal on purpose to avoid destabilizing the confirmed-stable
// network features. The next commit will fill in the actual HID plumbing.

#include "usb/usb_host.h"

// Placeholder task for now
static void usb_ups_task(void *arg) {
    (void)arg;

    ESP_LOGI(TAG, "USB host enabled. Starting skeleton USB UPS task.");
    ESP_LOGI(TAG, "Target UPS VID:PID %04x:%04x", UPS_USB_VID, UPS_USB_PID);

    // Install USB Host Library (basic)
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = 0,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "usb_host_install OK (skeleton).");
    ESP_LOGW(TAG, "USB HID attach/claim/interrupt streaming not implemented yet in this module skeleton.");

    // Keep USB host alive. Future: run client event loop and handle attach/detach.
    while (1) {
        // In a full implementation we'll call usb_host_lib_handle_events() and client handlers here.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void ups_usb_hid_start(const app_cfg_t *cfg) {
    (void)cfg;
    xTaskCreate(usb_ups_task, "usb_ups", 8192, NULL, 6, NULL);
}

#else

static void usb_ups_task_stub(void *arg) {
    (void)arg;
    ESP_LOGW(TAG, "USB Host is not enabled in menuconfig. USB UPS module is idle.");
    vTaskDelete(NULL);
}

void ups_usb_hid_start(const app_cfg_t *cfg) {
    (void)cfg;
    xTaskCreate(usb_ups_task_stub, "usb_ups_stub", 2048, NULL, 1, NULL);
}

#endif
