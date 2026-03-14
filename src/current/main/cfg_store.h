/*============================================================================
 MODULE: cfg_store (public API)

 REVERT HISTORY
 R0  v14.7  modular baseline public interface
 R1  v14.25 adds portal_pass field for HTTP portal access protection
 R2  v14.25 portal_pass defaults to "upsmon"; cfg_store_is_default_pass()

============================================================================*/

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char sta_ssid[33];
    char sta_pass[65];

    char ap_ssid[33];
    char ap_pass[65]; /* empty -> open AP */

    char ups_name[33];
    char nut_user[33];
    char nut_pass[33];

    char portal_pass[33]; /* default: "upsmon" — change via /config */
} app_cfg_t;

void      cfg_store_load_or_defaults(app_cfg_t *cfg);
void      cfg_store_ensure_ap_ssid(app_cfg_t *cfg);
esp_err_t cfg_store_commit(const app_cfg_t *cfg);

/* Returns true if portal_pass is still the factory default "upsmon".
 * Used by http_portal to display a change-password recommendation. */
bool      cfg_store_is_default_pass(const app_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
