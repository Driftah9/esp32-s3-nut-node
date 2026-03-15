/*============================================================================
 MODULE: wifi_mgr

 RESPONSIBILITY
 - AP+STA Wi-Fi bring-up
 - SoftAP lifecycle: disable when STA connected, restore after dropout
 - STA got-IP event tracking

 REVERT HISTORY
 R0  v14.7 modular baseline (real implementation)
 R1  v14.25 SoftAP lifecycle management:
            - SoftAP disabled after STA gets IP (no more open portal
              visible on the network once the device is configured)
            - If STA drops and stays down for WIFI_MGR_AP_RESTORE_SECS
              (60s), SoftAP re-enabled so device is recoverable
            - SoftAP disabled again immediately on STA reconnect
            - wifi_mgr_ap_is_active() exposed for portal to check

============================================================================*/

#include "wifi_mgr.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"

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

/* SoftAP lifecycle state */
static volatile bool s_ap_active = true;   /* starts true; APSTA mode on boot */
static TimerHandle_t s_ap_restore_timer = NULL;

/* Forward declaration */
static void ap_restore_timer_cb(TimerHandle_t xTimer);

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void configure_ap_ip(void) {
    if (!s_ap_netif) return;

    esp_netif_ip_info_t ip = {0};
    ip4addr_aton(WIFI_MGR_SOFTAP_IP_STR,     (ip4_addr_t *)&ip.ip);
    ip4addr_aton(WIFI_MGR_SOFTAP_GW_STR,      (ip4_addr_t *)&ip.gw);
    ip4addr_aton(WIFI_MGR_SOFTAP_NETMASK_STR, (ip4_addr_t *)&ip.netmask);

    esp_netif_dhcps_stop(s_ap_netif);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_ip_info(s_ap_netif, &ip));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(s_ap_netif));
}

static void disable_softap(void) {
    if (!s_ap_active) return;
    ESP_LOGI(TAG, "STA connected — disabling SoftAP.");
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_ap_active = false;
}

static void enable_softap(void) {
    if (s_ap_active) return;
    ESP_LOGW(TAG, "STA absent %ds — re-enabling SoftAP for recovery.",
             WIFI_MGR_AP_RESTORE_SECS);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    s_ap_active = true;
}

static void ap_restore_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    /* Only restore if STA still has no IP */
    if (!wifi_mgr_sta_has_ip()) {
        enable_softap();
    }
}

/* -------------------------------------------------------------------------
 * Wi-Fi event router
 * ---------------------------------------------------------------------- */

static void wifi_event_router(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;

    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "STA start");
            esp_wifi_connect();

        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_STA_GOT_IP_BIT);
            ESP_LOGW(TAG, "STA disconnected — starting AP restore timer (%ds).",
                     WIFI_MGR_AP_RESTORE_SECS);

            /* Re-try STA immediately */
            esp_wifi_connect();

            /* Start (or restart) the AP restore timer */
            if (s_ap_restore_timer) {
                xTimerStop(s_ap_restore_timer, 0);
                xTimerChangePeriod(s_ap_restore_timer,
                    pdMS_TO_TICKS(WIFI_MGR_AP_RESTORE_SECS * 1000), 0);
                xTimerStart(s_ap_restore_timer, 0);
            }

        } else if (id == WIFI_EVENT_AP_START) {
            ESP_LOGI(TAG, "SoftAP started — portal: http://%s/config",
                     WIFI_MGR_SOFTAP_IP_STR);
        }

    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
            char ip[16];
            inet_ntoa_r(e->ip_info.ip, ip, sizeof(ip));
            ESP_LOGI(TAG, "STA got IP: %s", ip);
            xEventGroupSetBits(s_wifi_event_group, WIFI_STA_GOT_IP_BIT);

            /* Cancel any pending restore timer */
            if (s_ap_restore_timer) {
                xTimerStop(s_ap_restore_timer, 0);
            }

            /* Disable SoftAP now that we're on the network */
            disable_softap();
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

EventGroupHandle_t wifi_mgr_event_group(void) { return s_wifi_event_group; }

bool wifi_mgr_sta_has_ip(void) {
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_STA_GOT_IP_BIT) != 0;
}

bool wifi_mgr_ap_is_active(void) {
    return s_ap_active;
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

    /* One-shot timer; fires WIFI_MGR_AP_RESTORE_SECS after STA dropout */
    s_ap_restore_timer = xTimerCreate(
        "ap_restore",
        pdMS_TO_TICKS(WIFI_MGR_AP_RESTORE_SECS * 1000),
        pdFALSE,   /* one-shot */
        NULL,
        ap_restore_timer_cb
    );

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    configure_ap_ip();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,  &wifi_event_router, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, &wifi_event_router, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    /* --- SoftAP config --- */
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

    /* --- STA config --- */
    wifi_config_t sta_cfg = {0};
    if (cfg->sta_ssid[0]) {
        strlcpy0((char*)sta_cfg.sta.ssid,     cfg->sta_ssid, sizeof(sta_cfg.sta.ssid));
        strlcpy0((char*)sta_cfg.sta.password, cfg->sta_pass, sizeof(sta_cfg.sta.password));
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,  &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_active = true;
    ESP_LOGI(TAG, "SoftAP ready: SSID '%s' -> http://%s/config", cfg->ap_ssid, WIFI_MGR_SOFTAP_IP_STR);
}
