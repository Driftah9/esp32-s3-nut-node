/* MODULE cfg_store | REVERT R0 v14.7 modular baseline */
#include "cfg_store.h"
#include <string.h>
void cfg_store_load_or_defaults(app_cfg_t *c){memset(c,0,sizeof(*c));strcpy(c->ups_name,"ups");strcpy(c->nut_user,"admin");strcpy(c->nut_pass,"admin");}
void cfg_store_ensure_ap_ssid(app_cfg_t *c){if(!c->ap_ssid[0])strcpy(c->ap_ssid,"ESP32-UPS-SETUP");}
int cfg_store_commit(const app_cfg_t *c){return 0;}
