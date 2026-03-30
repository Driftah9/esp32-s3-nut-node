/*============================================================================
 MODULE: ups_db_standard

 Standard HID path devices - vendors whose UPS models follow the HID PDC
 spec closely enough that no custom decode logic is needed. Minor quirk
 flags (exponent fixes, GET_REPORT flags) are the only deviations.

 Mirrors NUT's belkin-hid.c which combines Belkin and Liebert in one file
 because both share the same standard-HID approach with only minor quirks.

 Vendors covered:
   Tripp Lite   VID 0x09AE
   Belkin       VID 0x050D
   Liebert      VID 0x10AF
   HP           VID 0x03F0
   Dell         VID 0x047C

 VERSION HISTORY
 R0  v15.17  Extracted from ups_device_db.c.
============================================================================*/
#pragma once
#include "ups_device_db.h"
#include <stddef.h>

const ups_device_entry_t *ups_db_standard_get_entries(size_t *out_count);
