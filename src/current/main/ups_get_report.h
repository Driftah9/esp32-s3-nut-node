/*============================================================================
 MODULE: ups_get_report (public API)

 RESPONSIBILITY
 - Poll USB Feature reports (GET_REPORT) for UPS devices that require them
 - Only started when the device DB entry has QUIRK_NEEDS_GET_REPORT set
 - Feeds decoded data into ups_state via ups_hid_parse_feature_report()
 - Stopped automatically on USB disconnect

 Supported devices (QUIRK_NEEDS_GET_REPORT):
   APC Back-UPS    (VID=051D PID=0002)  — battery/input/output voltages
   Tripp Lite      (VID=09AE)           — various Feature-report-only values

 DESIGN
 - ups_usb_hid calls ups_get_report_start() after USB enumeration if quirk set
 - ups_usb_hid calls ups_get_report_stop()  on USB disconnect
 - A FreeRTOS task polls Feature reports every UPS_GET_REPORT_INTERVAL_MS
 - Uses the same USB client handle owned by ups_usb_hid (passed in at start)
 - Report IDs to poll are per decode_mode (APC vs Tripp Lite differ)

 VERSION HISTORY
 R0  v15.8  Initial implementation — APC Back-UPS Feature report polling.

============================================================================*/
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "usb/usb_host.h"
#include "ups_device_db.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UPS_GET_REPORT_INTERVAL_MS  30000   /* poll Feature reports every 30s */

/**
 * Start the GET_REPORT polling task.
 *
 * Called by ups_usb_hid after USB enumeration when device has
 * QUIRK_NEEDS_GET_REPORT set in the device DB.
 *
 * @param client   USB host client handle (owned by ups_usb_hid)
 * @param dev      USB device handle
 * @param intf_num HID interface number
 * @param entry    Device DB entry (decode mode + quirks)
 */
void ups_get_report_start(usb_host_client_handle_t client,
                          usb_device_handle_t      dev,
                          int                      intf_num,
                          const ups_device_entry_t *entry);

/**
 * Stop the GET_REPORT polling task.
 * Safe to call even if the task was never started.
 */
void ups_get_report_stop(void);

/** True if the GET_REPORT task is currently running. */
bool ups_get_report_running(void);

/**
 * Service the GET_REPORT request queue — MUST be called from usb_client_task.
 *
 * Drains up to one pending request, issues the control transfer, and decodes
 * the result. This runs inside usb_client_task (the USB client handle owner),
 * so control transfers are single-threaded — no concurrency hazard.
 *
 * Called on every iteration of the usb_client_task event loop, after
 * usb_host_client_handle_events() returns (with a short timeout).
 */
void ups_get_report_service_queue(void);

#ifdef __cplusplus
}
#endif
