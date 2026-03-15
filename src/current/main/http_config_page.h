/*============================================================================
 MODULE: http_config_page

 RESPONSIBILITY
 - Renders GET /config -- configuration form page
 - Parses POST /save form body
 - Split from http_portal.c v15.11

 REVERT HISTORY
 R0  v15.11  Split from http_portal.c
============================================================================*/

#pragma once
#include "cfg_store.h"
#include <stddef.h>

void render_config(app_cfg_t *cfg, char *out, size_t outsz,
                   const char *note, const char *note_cls);

void parse_form_kv(app_cfg_t *cfg_inout, const char *body,
                   char *action_out, size_t action_sz);
