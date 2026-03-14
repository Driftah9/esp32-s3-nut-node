/*============================================================================
 MODULE: ups_usb_hid (public API)

 RESPONSIBILITY
 - USB Host lifecycle
 - Enumerate attached USB devices (on boot + hotplug)
 - Parse HID interface and stream interrupt reports
 - Feed decoded reports into ups_state via ups_hid_parser

 REVERT HISTORY
 R0  v14.7 USB skeleton API
 R1  v14.8 scan + identify API
 R2  v14.9 parser/state integration

============================================================================*/
#pragma once

#include "cfg_store.h"

#ifdef __cplusplus
extern "C" {
#endif

void ups_usb_hid_start(const app_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
