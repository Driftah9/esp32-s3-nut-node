/*============================================================================
 MODULE: ups_device_db

 PURPOSE
 Device database — keyed by VID:PID (or VID-only wildcard).
 Carries per-device quirk flags, decode mode, and log name.

 DESIGN
 - Standard HID path is always attempted first for all devices.
 - Quirk flags patch descriptor issues BEFORE field cache is built.
 - QUIRK_DIRECT_DECODE replaces the standard path entirely for devices
   whose descriptors are so broken that the standard path yields nothing
   useful (e.g. CyberPower PID=0x0501/0x0601).
 - Unknown devices fall through as VENDOR_GENERIC with no quirks — the
   standard HID descriptor path is used and a warning is logged.

 VERSION HISTORY
 R0  v15.4  Initial - extracted from ups_hid_parser.c hardcoded logic.
 R1  v15.12 Added NUT static fields per device:
            battery_voltage_nominal_mv, battery_runtime_low_s,
            battery_charge_low, battery_charge_warning,
            input_voltage_nominal_v, ups_type.
            Enables full NUT variable parity for confirmed devices.

============================================================================*/
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ---- Quirk flag bits -------------------------------------------------- */

/* Voltage LogMax is wrong (too large) — force logical range [0, 511] V     */
#define QUIRK_VOLTAGE_LOGMAX_FIX     (1u << 0)

/* Battery voltage sometimes reported 1.5× high — scale × 2/3 if ratio>1.4 */
#define QUIRK_BATT_VOLT_SCALE        (1u << 1)

/* Frequency reported ×10 too high (e.g. 499 → 49.9 Hz)                    */
#define QUIRK_FREQ_SCALE_0_1         (1u << 2)

/* ConfigActivePower LogMax too small — force LogMax = 2048                 */
#define QUIRK_ACTIVE_PWR_LOGMAX_FIX  (1u << 3)

/* Vendor uses 0xFF84/0xFF85 usage pages — normalise to 0x84/0x85           */
#define QUIRK_VENDOR_PAGE_REMAP      (1u << 4)

/* Descriptor so broken that standard field cache is useless —
   use the per-vendor direct-decode bypass instead                           */
#define QUIRK_DIRECT_DECODE          (1u << 5)

/* Uses Feature reports (GET_REPORT) for some values, not interrupt-IN      */
#define QUIRK_NEEDS_GET_REPORT       (1u << 6)

/* ---- Decode modes ---------------------------------------------------- */
typedef enum {
    DECODE_STANDARD     = 0,  /* Generic HID descriptor path (default)        */
    DECODE_CYBERPOWER   = 1,  /* CyberPower direct-decode bypass               */
    DECODE_APC_BACKUPS  = 2,  /* APC Back-UPS (PID 0002) direct-decode bypass  */
    DECODE_APC_SMARTUPS = 3,  /* APC Smart-UPS (PID 0003) direct-decode bypass */
    DECODE_EATON_MGE    = 4,  /* Eaton/MGE (PID FFFF) - GET_REPORT + undocumented INT-IN rids */
} ups_decode_mode_t;

/* ---- Database entry -------------------------------------------------- */
typedef struct {
    uint16_t           vid;          /* USB Vendor ID  (0 = wildcard/any)   */
    uint16_t           pid;          /* USB Product ID (0 = VID-only match) */
    const char        *vendor_name;  /* Human-readable vendor name          */
    const char        *model_hint;   /* Model family hint (may be NULL)     */
    ups_decode_mode_t  decode_mode;  /* How to decode interrupt-IN reports  */
    uint32_t           quirks;       /* QUIRK_* bitmask                     */
    bool               known_good;   /* true = confirmed working standard   */

    /* ---- NUT static variables ----------------------------------------
     * These are served as NUT LIST VAR responses even when the live
     * decode path cannot read them from the device in real time.
     * Values sourced from NUT DDL / device datasheets.
     * 0 = not known / do not serve this variable for this device.
     */
    uint32_t  battery_voltage_nominal_mv;  /* battery.voltage.nominal (mV) */
    uint32_t  battery_runtime_low_s;       /* battery.runtime.low (seconds) */
    uint8_t   battery_charge_low;          /* battery.charge.low (%)        */
    uint8_t   battery_charge_warning;      /* battery.charge.warning (%)    */
    uint16_t  input_voltage_nominal_v;     /* input.voltage.nominal (V)     */
    const char *ups_type;                  /* ups.type string, NULL=default */
} ups_device_entry_t;

/* ---- Public API ------------------------------------------------------ */

/**
 * Look up a device by VID:PID.
 * Returns a pointer to the matching entry, or the generic fallback entry.
 * Never returns NULL.
 *
 * Match priority:
 *   1. Exact VID:PID match
 *   2. VID-only match  (pid == 0 in table)
 *   3. Generic fallback
 */
const ups_device_entry_t *ups_device_db_lookup(uint16_t vid, uint16_t pid);

/** Log the matched entry at INFO level. */
void ups_device_db_log(const ups_device_entry_t *entry, uint16_t vid, uint16_t pid);
