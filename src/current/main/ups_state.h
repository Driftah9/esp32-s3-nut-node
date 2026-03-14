/*============================================================================
 MODULE: ups_state (public API)

 REVERT HISTORY
 R0  v14.7  modular + USB skeleton public interface
 R1  v14.9  expanded UPS metric/state interface
 R2  v14.10 adds candidate decode fields for voltage/load
 R3  v14.16 adds ups_state_on_usb_disconnect()
 R4  v14.21 adds ups_model_hint_t enum + ups_state_get_model_hint()
 R5  v14.23 adds battery_runtime_valid flag
 R6  v14.24 adds static/derived fields, ups_firmware, battery_charge_low
 R7  v15.0  Remove ups_model_hint_t — model detection is no longer needed.
            The HID descriptor parser is now usage-based and vendor-agnostic.
            ups_state_get_model_hint() removed.
            ups_state_set_usb_identity() signature unchanged for compatibility.
 R8  v15.2  Add ups_state_get_vid_pid() — needed by ups_hid_parser for
            descriptor cross-check logging.
 R9  v15.7  Remove input_voltage and output_voltage fields — not available
            on any currently tested device via interrupt IN reports.
            APC voltages are Feature-report-only (GET_REPORT, future task).
 R10 v15.8  Re-add input_voltage_mv and output_voltage_mv — now populated by
            ups_get_report.c Feature report polling for QUIRK_NEEDS_GET_REPORT
            devices. Fields remain absent from interrupt-IN path.

============================================================================*/
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Live HID-decoded fields */
    uint8_t  battery_charge;
    uint32_t battery_runtime_s;
    bool     battery_runtime_valid;
    bool     input_utility_present;
    uint32_t ups_flags;
    char     ups_status[16];   /* compound: "OL CHRG", "OB DISCHRG", "OL LB" etc. */

    uint32_t battery_voltage_mv;
    uint8_t  ups_load_pct;

    bool     battery_voltage_valid;
    bool     ups_load_valid;

    /* Feature-report-only fields (populated by ups_get_report.c) */
    uint32_t input_voltage_mv;
    bool     input_voltage_valid;
    uint32_t output_voltage_mv;
    bool     output_voltage_valid;

    /* Static / derived at enumeration time */
    char     ups_firmware[32];       /* e.g. "947.d10" or "unknown" */
    uint8_t  battery_charge_low;     /* low-battery threshold (%) — default 20 */

    /* USB identity */
    uint16_t vid;
    uint16_t pid;
    uint16_t hid_report_desc_len;
    char     manufacturer[64];
    char     product[64];
    char     serial[64];

    bool     valid;
    uint32_t last_update_ms;
} ups_state_t;

typedef struct {
    bool     valid;
    bool     battery_charge_valid;
    uint8_t  battery_charge;

    bool     battery_runtime_valid;
    uint32_t battery_runtime_s;

    bool     input_utility_present_valid;
    bool     input_utility_present;

    bool     ups_flags_valid;
    uint32_t ups_flags;

    bool     battery_voltage_valid;
    uint32_t battery_voltage_mv;

    bool     ups_load_valid;
    uint8_t  ups_load_pct;

    bool     input_voltage_valid;
    uint32_t input_voltage_mv;

    bool     output_voltage_valid;
    uint32_t output_voltage_mv;

    char     ups_status[16];
} ups_state_update_t;

void ups_state_init(ups_state_t *st);
void ups_state_set_demo_defaults(ups_state_t *st);
void ups_state_on_usb_disconnect(void);
void ups_state_snapshot(ups_state_t *dst);
void ups_state_apply_update(const ups_state_update_t *upd);
void ups_state_set_usb_identity(uint16_t vid, uint16_t pid, uint16_t hid_report_desc_len,
                                const char *manufacturer, const char *product,
                                const char *serial);
void ups_state_get_vid_pid(uint16_t *vid_out, uint16_t *pid_out);

#ifdef __cplusplus
}
#endif
