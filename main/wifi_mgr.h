/*============================================================================
 MODULE: wifi_mgr (public API)

 REVERT HISTORY
 R0  v14.7 modular baseline public interface
 R1  v14.10 include-order fix for FreeRTOS event groups
 R2  v14.25 SoftAP lifecycle: disable on STA connect, re-enable after
            60s STA dropout, disable again on STA reconnect.
            Adds wifi_mgr_ap_is_active() query.

============================================================================*/

#pragma once

#include "cfg_store.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define WIFI_MGR_SOFTAP_IP_STR      "192.168.4.1"
#define WIFI_MGR_SOFTAP_NETMASK_STR "255.255.255.0"
#define WIFI_MGR_SOFTAP_GW_STR      "192.168.4.1"

/* Seconds of STA absence before SoftAP is re-enabled */
#define WIFI_MGR_AP_RESTORE_SECS    60

EventGroupHandle_t wifi_mgr_event_group(void);
bool wifi_mgr_sta_has_ip(void);
bool wifi_mgr_ap_is_active(void);
const char *wifi_mgr_sta_ip_str(char out[16]);

void wifi_mgr_start_apsta(const app_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
