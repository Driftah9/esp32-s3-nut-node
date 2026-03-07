/*============================================================================
 MODULE: ups_state (public API)

 REVERT HISTORY
 R0  v14.7 modular + USB skeleton public interface
 R1  v14.9 expanded UPS metric/state interface
 R2  v14.10 adds candidate decode fields for voltage/load
 R3  v14.16 adds ups_state_on_usb_disconnect()
 R4  v14.21 adds ups_model_hint_t enum + ups_state_get_model_hint()
 R5  v14.23 adds battery_runtime_valid flag
 R6  v14.24 adds static/derived fields for broader APC HID compatibility:
            - battery.charge.low  (per-model static threshold)
            - battery.type        (static "PbAc" for all APC Back-UPS)
            - ups.type            (static "line-interactive" for Back-UPS)
            - ups.firmware        (extracted from product string)
            - ups_status expanded to 16 bytes for compound tokens
              e.g. "OL CHRG", "OB DISCHRG", "OL LB"

============================================================================*/
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UPS_MODEL_UNKNOWN  = 0,
    UPS_MODEL_BR1000G  = 1,
    UPS_MODEL_XS1500M  = 2,
} ups_model_hint_t;

typedef struct {
    /* Live HID-decoded fields */
    uint8_t  battery_charge;
    uint32_t battery_runtime_s;
    bool     battery_runtime_valid;
    bool     input_utility_present;
    uint32_t ups_flags;
    char     ups_status[16];           /* compound: "OL CHRG", "OB DISCHRG", "OL LB" etc. */

    uint32_t battery_voltage_mv;
    uint32_t input_voltage_mv;
    uint32_t output_voltage_mv;
    uint8_t  ups_load_pct;

    bool     battery_voltage_valid;
    bool     input_voltage_valid;
    bool     output_voltage_valid;
    bool     ups_load_valid;

    /* Static / derived at enumeration time */
    char     ups_firmware[32];         /* e.g. "947.d10" extracted from product string */
    uint8_t  battery_charge_low;       /* per-model low-battery threshold (%) */
    /* ups.type and battery.type are static strings served directly in nut_server */

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

    bool     input_voltage_valid;
    uint32_t input_voltage_mv;

    bool     output_voltage_valid;
    uint32_t output_voltage_mv;

    bool     ups_load_valid;
    uint8_t  ups_load_pct;

    char     ups_status[16];
} ups_state_update_t;

void ups_state_init(ups_state_t *st);
void ups_state_set_demo_defaults(ups_state_t *st);
void ups_state_on_usb_disconnect(void);
void ups_state_snapshot(ups_state_t *dst);
void ups_state_apply_update(const ups_state_update_t *upd);
void ups_state_set_usb_identity(uint16_t vid, uint16_t pid, uint16_t hid_report_desc_len,
                                const char *manufacturer, const char *product, const char *serial);
ups_model_hint_t ups_state_get_model_hint(void);

#ifdef __cplusplus
}
#endif
