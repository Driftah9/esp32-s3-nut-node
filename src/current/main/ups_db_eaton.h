/*============================================================================
 MODULE: ups_db_eaton

 Eaton / MGE / Powerware device table entries (VID 0x0463).
 Included by ups_device_db.c via ups_db_eaton_get_entries().

 VERSION HISTORY
 R0  v15.17  Extracted from ups_device_db.c. Added confirmed Eaton 3S entry
             (PID 0xFFFF, DECODE_EATON_MGE, battery.charge confirmed via
             GET_REPORT rid=0x20, two community submissions 2026-03-30).
============================================================================*/
#pragma once
#include "ups_device_db.h"
#include <stddef.h>

const ups_device_entry_t *ups_db_eaton_get_entries(size_t *out_count);
