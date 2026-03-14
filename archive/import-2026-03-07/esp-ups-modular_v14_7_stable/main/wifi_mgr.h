#pragma once
#include "cfg_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "freertos/event_groups.h"

#define WIFI_MGR_SOFTAP_IP_STR      "192.168.4.1"
#define WIFI_MGR_SOFTAP_NETMASK_STR "255.255.255.0"
#define WIFI_MGR_SOFTAP_GW_STR      "192.168.4.1"

EventGroupHandle_t wifi_mgr_event_group(void);
bool wifi_mgr_sta_has_ip(void);
const char *wifi_mgr_sta_ip_str(char out[16]);

void wifi_mgr_start_apsta(const app_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
