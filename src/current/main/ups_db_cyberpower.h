/*============================================================================
 MODULE: ups_db_cyberpower

 CyberPower Systems device table entries (VID 0x0764).
 Included by ups_device_db.c via ups_db_cyberpower_get_entries().

 VERSION HISTORY
 R0  v15.17  Extracted from ups_device_db.c.
============================================================================*/
#pragma once
#include "ups_device_db.h"
#include <stddef.h>

const ups_device_entry_t *ups_db_cyberpower_get_entries(size_t *out_count);
