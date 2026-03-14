/*============================================================================
 MODULE: ups_state (public API)

 REVERT HISTORY
 R0  v14.7 modular + USB skeleton public interface
 R1  v14.9 expanded UPS metric/state interface

============================================================================*/
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Core real-time UPS values
    uint8_t  battery_charge;      // 0-100
    uint32_t battery_runtime_s;   // seconds
    bool     input_utility_present;
    uint32_t ups_flags;           // bitfield (vendor/decoder-defined)
    char     ups_status[8];       // e.g. "OL", "OB", etc.

    // Expanded metrics (become valid when parser can decode them)
    uint32_t battery_voltage_mv;
    uint32_t input_voltage_mv;
    uint32_t output_voltage_mv;
    uint8_t  ups_load_pct;

    bool     battery_voltage_valid;
    bool     input_voltage_valid;
    bool     output_voltage_valid;
    bool     ups_load_valid;

    // USB identity / discovery
    uint16_t vid;
    uint16_t pid;
    uint16_t hid_report_desc_len;
    char     manufacturer[64];
    char     product[64];
    char     serial[64];

    // Meta
    bool     valid;               // true once populated from actual USB HID reports
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

    char     ups_status[8];
} ups_state_update_t;

void ups_state_init(ups_state_t *st);
void ups_state_set_demo_defaults(ups_state_t *st);

// Thread-safe access helpers
void ups_state_snapshot(ups_state_t *dst);

// Update helpers
void ups_state_apply_update(const ups_state_update_t *upd);
void ups_state_set_usb_identity(uint16_t vid, uint16_t pid, uint16_t hid_report_desc_len,
                                const char *manufacturer, const char *product, const char *serial);

#ifdef __cplusplus
}
#endif
