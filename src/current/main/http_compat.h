/*============================================================================
 MODULE: http_compat

 RESPONSIBILITY
 - Renders GET /compat — Compatible UPS device list page
 - Two-level expandable hierarchy: Vendor -> Series -> Model table
 - All data sourced from NUT usbhid-ups driver hardware compatibility list
 - Split from http_portal.c to keep that file editable

 REVERT HISTORY
 R0  v15.10  Split from http_portal.c
============================================================================*/

#pragma once

#include <stddef.h>

/* Buffer size required by render_compat — caller must allocate this */
#define HTTP_COMPAT_BUF 49152

/*
 * Render the /compat page into out[0..outsz].
 * Caller allocates: char *page = malloc(HTTP_COMPAT_BUF);
 */
void render_compat(char *out, size_t outsz);
