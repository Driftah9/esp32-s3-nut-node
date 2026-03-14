/*============================================================================
 MODULE: cfg_store (public API)

 REVERT HISTORY
 R0  v14.7 modular baseline public interface

============================================================================*/

#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char sta_ssid[33];
    char sta_pass[65];

    char ap_ssid[33];
    char ap_pass[65]; // empty -> open auth

    char ups_name[33];
    char nut_user[33];
    char nut_pass[33];
} app_cfg_t;

void cfg_store_load_or_defaults(app_cfg_t *cfg);
void cfg_store_ensure_ap_ssid(app_cfg_t *cfg);
esp_err_t cfg_store_commit(const app_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
