#include "cfg_store.h"

#include <string.h>
#include <stdio.h>

#include "nvs.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "cfg_store";

#define NVS_NS_CFG "cfg"

static void strlcpy0(char *dst, const char *src, size_t dstsz) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
}

static void make_default_ap_ssid(char out[33]) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, 33, "ESP32-UPS-SETUP-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static esp_err_t cfg_load_raw(app_cfg_t *cfg) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_CFG, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t len = sizeof(*cfg);
    err = nvs_get_blob(h, "cfg", cfg, &len);
    nvs_close(h);
    return err;
}

esp_err_t cfg_store_commit(const app_cfg_t *cfg) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_CFG, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "cfg", cfg, sizeof(*cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

void cfg_store_load_or_defaults(app_cfg_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strlcpy0(cfg->ups_name, "ups", sizeof(cfg->ups_name));
    strlcpy0(cfg->nut_user, "admin", sizeof(cfg->nut_user));
    strlcpy0(cfg->nut_pass, "admin", sizeof(cfg->nut_pass));

    esp_err_t err = cfg_load_raw(cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS cfg not found yet (first boot). Using defaults.");
    }
}

void cfg_store_ensure_ap_ssid(app_cfg_t *cfg) {
    if (!cfg->ap_ssid[0]) {
        make_default_ap_ssid(cfg->ap_ssid);
        cfg->ap_pass[0] = 0;
        ESP_LOGI(TAG, "Generated default AP SSID: %s", cfg->ap_ssid);
    }
}
