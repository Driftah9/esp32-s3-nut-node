/*============================================================================
 MODULE: ups_hid_descriptor_map (public API)

 RESPONSIBILITY
 - Lightweight HID report descriptor walker for ESP32 UPS project
 - Logs report descriptor structure to help identify APC/NUT-relevant fields
 - Intended for v14.5 descriptor-driven mapping work

 STATUS
 - Inspection artifact
 - Safe to add without changing current NUT export behavior

============================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ups_hid_descriptor_map_log(const uint8_t *desc, size_t len);

#ifdef __cplusplus
}
#endif
