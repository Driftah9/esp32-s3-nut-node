/*============================================================================
 MODULE: http_dashboard

 RESPONSIBILITY
 - Renders GET / -- UPS status dashboard with AJAX live polling
 - Split from http_portal.c v15.11

 REVERT HISTORY
 R0  v15.11  Split from http_portal.c
============================================================================*/

#pragma once
#include "cfg_store.h"
#include <stddef.h>

#define HTTP_PAGE_BUF 16384

void render_dashboard(app_cfg_t *cfg, char *out, size_t outsz);
