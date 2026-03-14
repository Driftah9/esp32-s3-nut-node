/* MODULE cfg_store | REVERT R0 v14.7 modular baseline */
#pragma once
typedef struct {char sta_ssid[33];char sta_pass[65];char ap_ssid[33];char ap_pass[65];char ups_name[33];char nut_user[33];char nut_pass[33];} app_cfg_t;
void cfg_store_load_or_defaults(app_cfg_t*);
void cfg_store_ensure_ap_ssid(app_cfg_t*);
int cfg_store_commit(const app_cfg_t*);
