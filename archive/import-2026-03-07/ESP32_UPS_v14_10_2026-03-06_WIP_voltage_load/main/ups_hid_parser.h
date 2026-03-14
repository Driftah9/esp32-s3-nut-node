/*============================================================================
 MODULE: ups_hid_parser (public API)

 REVERT HISTORY
 R0  v14.9 parser scaffold
 R1  v14.10 candidate voltage/load decoding and debug hooks

============================================================================*/
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ups_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void ups_hid_parser_reset(void);
bool ups_hid_parser_decode_report(const uint8_t *data, size_t len, ups_state_update_t *upd);

#ifdef __cplusplus
}
#endif
