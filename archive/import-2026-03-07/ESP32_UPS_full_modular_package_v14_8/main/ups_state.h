/*============================================================================
 MODULE: ups_state (public API)

 REVERT HISTORY
 R0  v14.7 modular + USB skeleton public interface

============================================================================*/

#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Real-time UPS values (updated by USB/HID module)
    uint8_t  battery_charge;      // 0-100
    uint32_t battery_runtime_s;   // seconds
    bool     input_utility_present;
    uint32_t ups_flags;           // bitfield (vendor/decoder-defined)
    char     ups_status[8];       // e.g. "OL", "OB", etc.

    // Meta
    bool     valid;               // true once populated from actual USB HID reports
    uint32_t last_update_ms;
} ups_state_t;

void ups_state_init(ups_state_t *st);
void ups_state_set_demo_defaults(ups_state_t *st); // matches current v14.7 static NUT output

// Thread-safe access helpers (simple critical section)
void ups_state_snapshot(ups_state_t *dst);

#ifdef __cplusplus
}
#endif
