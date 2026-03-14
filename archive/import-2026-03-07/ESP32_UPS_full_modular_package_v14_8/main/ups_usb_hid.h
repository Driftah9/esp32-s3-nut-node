/*============================================================================
 MODULE: ups_usb_hid (public API)

 REVERT HISTORY
 R0  v14.7 USB skeleton API
 R1  v14.8 scan + identify API

============================================================================*/

\
/*============================================================================
 MODULE: ups_usb_hid (public API)

 RESPONSIBILITY
 - USB Host lifecycle
 - Enumerate attached USB devices (on boot + hotplug)
 - Select HID candidates and extract identity + HID report descriptor length
 - (Next) Claim HID interface, stream interrupt IN, decode into ups_state

 CURRENT STATE (v14.8 step)
 - Implements: enumerate → open → parse active config → find HID intf + IN EP →
   read HID report descriptor length (0x21) → optional report descriptor dump.
 - Does NOT yet:
   - decode reports into ups_state (beyond placeholder)
   - support multiple simultaneous devices (1 active device at a time)

 REVERT HISTORY
 R0  v14.7 USB skeleton placeholder
 R1  v14.8 USB scan + identify + HID descriptor discovery (this file)

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
