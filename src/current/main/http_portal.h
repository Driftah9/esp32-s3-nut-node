/*============================================================================
 MODULE: http_portal (public API)

 REVERT HISTORY
 R0  v14.7 modular baseline public interface

============================================================================*/

#pragma once
#include "cfg_store.h"

#ifdef __cplusplus
extern "C" {
#endif

void http_portal_start(app_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
