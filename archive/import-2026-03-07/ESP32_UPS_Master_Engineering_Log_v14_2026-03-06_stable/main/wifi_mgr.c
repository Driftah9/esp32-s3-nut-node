/*============================================================================
 MODULE: wifi_mgr

 RESPONSIBILITY
 - AP+STA Wi-Fi bring-up
 - SoftAP config portal IP helpers
 - STA got-IP event tracking

 REVERT HISTORY
 R0  v14.7 modular baseline (real implementation)

============================================================================*/

#include "wifi_mgr.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_err.h"

#include "lwip/inet.h"

static const char *TAG = "wifi_mgr";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_STA_GOT_IP_BIT BIT0

static esp_netif_t *s_ap_netif  = NULL;
static esp_netif_t *s_sta_netif = NULL;

static void configure_ap_ip(void) {
    if (!s_ap_netif) return;

    esp_netif_ip_info_t ip = {0};
    ip4addr_aton(WIFI_MGR_SOFTAP_IP_STR, &ip.ip);
    ip4addr_aton(WIFI_MGR_SOFTAP_GW_STR, &ip.gw);
    ip4addr_aton(WIFI_MGR_SOFTAP_NETMASK_STR, &ip.netmask);

    esp_netif_dhcps_stop(s_ap_netif);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_ip_info(s_ap_netif, &ip));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(s_ap_netif));
}

static void wifi_event_router(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;

    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "STA start");
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_STA_GOT_IP_BIT);
            ESP_LOGW(TAG, "STA disconnected.");
        } else if (id == WIFI_EVENT_AP_START) {
            ESP_LOGI(TAG, "SoftAP started (config portal).");
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
            char ip[16];
            inet_ntoa_r(e->ip_info.ip, ip, sizeof(ip));
            ESP_LOGI(TAG, "STA got IP: %s", ip);
            xEventGroupSetBits(s_wifi_event_group, WIFI_STA_GOT_IP_BIT);
        }
    }
}

EventGroupHandle_t wifi_mgr_event_group(void) { return s_wifi_event_group; }

bool wifi_mgr_sta_has_ip(void) {
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_STA_GOT_IP_BIT) != 0;
}

const char *wifi_mgr_sta_ip_str(char out[16]) {
    out[0] = 0;
    if (!s_sta_netif) return out;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_sta_netif, &ip) != ESP_OK) return out;
    inet_ntoa_r(ip.ip, out, 16);
    return out;
}

static void strlcpy0(char *dst, const char *src, size_t dstsz) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
}

void wifi_mgr_start_apsta(const app_cfg_t *cfg) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    configure_ap_ip();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_router, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_router, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap_cfg = {0};
    strlcpy0((char*)ap_cfg.ap.ssid, cfg->ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = (uint8_t)strlen((char*)ap_cfg.ap.ssid);
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.max_connection = 4;

    bool ap_has_pass = (cfg->ap_pass[0] != 0);
    if (ap_has_pass && strlen(cfg->ap_pass) < 8) {
        ESP_LOGW(TAG, "SoftAP password too short (<8); falling back to open AP.");
        ap_has_pass = false;
    }
    if (ap_has_pass) {
        strlcpy0((char*)ap_cfg.ap.password, cfg->ap_pass, sizeof(ap_cfg.ap.password));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_cfg.ap.password[0] = 0;
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    wifi_config_t sta_cfg = {0};
    if (cfg->sta_ssid[0]) {
        strlcpy0((char*)sta_cfg.sta.ssid, cfg->sta_ssid, sizeof(sta_cfg.sta.ssid));
        strlcpy0((char*)sta_cfg.sta.password, cfg->sta_pass, sizeof(sta_cfg.sta.password));
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP ready: SSID '%s' -> http://%s/config", cfg->ap_ssid, WIFI_MGR_SOFTAP_IP_STR);
}
