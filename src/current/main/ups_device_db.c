/*============================================================================
 MODULE: ups_device_db

 REVERT HISTORY
 R0  v15.4  Initial — VID:PID table with quirk flags.
             Sources: NUT cps-hid.c, apc-hid.c, mge-hid.c,
                      tripplite-hid.c, liebert-hid.c, hidtypes.h
             Confirmed live devices marked known_good=true.
 R1  v15.12 Added NUT static fields to all entries.
             Sources: NUT DDL (networkupstools/nut-ddl),
                      NUT HCL (networkupstools.org/stable-hcl.html),
                      confirmed device testing.
             Confirmed 3 devices (CP550HG, APC XS1500M, APC BR1000G).

============================================================================*/
#include "ups_device_db.h"
#include "esp_log.h"
#include <stddef.h>

static const char *TAG = "ups_device_db";

/* -----------------------------------------------------------------------
   Device table — ordered: specific PID first, VID-only wildcards last.
   pid == 0 means "match any PID for this VID".
   Add new entries above the VID-only wildcards.
----------------------------------------------------------------------- */
static const ups_device_entry_t s_db[] = {

    /* ----------------------------------------------------------------
       CyberPower (VID 0x0764)
       PID 0x0501: CP1200AVR, CP825AVR-G, CP1000AVRLCD, CP1500C,
                   CP550HG, CP1000PFCLCD, CP850PFCLCD, CP1350PFCLCD,
                   CP1500PFCLCD, CP1350AVRLCD, CP1500AVRLCD, CP900AVR,
                   CPS685AVR, CPS800AVR, EC350G, EC750G, EC850LCD,
                   BL1250U, AE550, CPJ500, SX550G (confirmed)
       PID 0x0601: OR2200LCDRM2U, OR700LCDRM1U, OR500LCDRM1U,
                   OR1500ERM1U, CP1350EPFCLCD, CP1500EPFCLCD,
                   PR1500RT2U, PR6000LCDRTXL5U, RT650EI, UT2200E,
                   CP1000PFCLCD, Value 1500ELCD-RU, VP1200ELCD
       Both PIDs: descriptor declares only ~2 Input fields on power
                  pages; all runtime data (rids 0x21–0x88) is on
                  vendor/undocumented pages → QUIRK_DIRECT_DECODE.
    ---------------------------------------------------------------- */
    {
        /* CyberPower AVR/SX/ST/CP consumer series — confirmed CP550HG/SX550G
         * NUT DDL: battery.voltage.nominal=12V, runtime.low=300s (rid=0x82)
         * battery.charge.low and charge.warning not in NUT DDL for CyberPower
         * — using conservative defaults. input.voltage.nominal=120V (US). */
        .vid         = 0x0764,
        .pid         = 0x0501,
        .vendor_name = "CyberPower",
        .model_hint  = "ST/CP/SX Series (PID 0501)",
        .decode_mode = DECODE_CYBERPOWER,
        .quirks      = QUIRK_DIRECT_DECODE |
                       QUIRK_VOLTAGE_LOGMAX_FIX |
                       QUIRK_BATT_VOLT_SCALE |
                       QUIRK_FREQ_SCALE_0_1,
        .known_good  = true,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
    {
        /* CyberPower OR/PR rackmount — same decode path as 0x0501
         * Rackmount units typically have 24V or 48V battery. Use 24V
         * as default; 48V models are less common in this PID range. */
        .vid         = 0x0764,
        .pid         = 0x0601,
        .vendor_name = "CyberPower",
        .model_hint  = "OR/PR/RT/UT Series (PID 0601)",
        .decode_mode = DECODE_CYBERPOWER,
        .quirks      = QUIRK_DIRECT_DECODE |
                       QUIRK_VOLTAGE_LOGMAX_FIX |
                       QUIRK_BATT_VOLT_SCALE |
                       QUIRK_FREQ_SCALE_0_1 |
                       QUIRK_ACTIVE_PWR_LOGMAX_FIX,
        .known_good  = true,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
    {
        /* Older CyberPower model */
        .vid         = 0x0764,
        .pid         = 0x0005,
        .vendor_name = "CyberPower",
        .model_hint  = "900AVR/BC900D (PID 0005)",
        .decode_mode = DECODE_STANDARD,
        .quirks      = QUIRK_VOLTAGE_LOGMAX_FIX,
        .known_good  = false,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
    {
        /* Cyber Energy / ST Micro OEM */
        .vid         = 0x0483,
        .pid         = 0xa430,
        .vendor_name = "CyberEnergy",
        .model_hint  = "USB Series (ST Micro OEM)",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
    {
        /* VID-only fallback for any other CyberPower PID */
        .vid         = 0x0764,
        .pid         = 0,
        .vendor_name = "CyberPower",
        .model_hint  = NULL,
        .decode_mode = DECODE_STANDARD,
        .quirks      = QUIRK_VOLTAGE_LOGMAX_FIX | QUIRK_FREQ_SCALE_0_1,
        .known_good  = false,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 300,
        .battery_charge_low         = 20,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* ----------------------------------------------------------------
       APC / Schneider (VID 0x051D)
       PID 0x0002: Back-UPS range (confirmed: XS 1500M)
         Live rids (from monitor log):
           rid=06  [3 bytes] byte0=charging(0/1), byte1=discharging(0/1),
                             byte2=status flags (bit3=?)
           rid=0C  [3 bytes] byte0=battery_charge%, byte1=runtime (units TBD),
                             byte2=? (0x8C observed)
           rid=13  [1 byte]  unknown
           rid=14  [2 bytes] unknown
           rid=16  [4 bytes] word16_le = something (0x000C=12 observed)
           rid=21  [1 byte]  unknown
         Descriptor has charging/discharging on rid=06 correctly but
         battery charge, runtime, voltages are on undocumented rids.
       PID 0x0000 wildcard: Smart-UPS may use standard path.
    ---------------------------------------------------------------- */
    {
        /* APC Back-UPS (PID 0x0002) — confirmed XS 1500M and BR1000G
         * NUT DDL: battery.voltage.nominal=24V, runtime.low=120s,
         * charge.low=10%, charge.warning=50%, input.voltage.nominal=120V.
         * Source: NUT DDL APC/Back-UPS XS 1500M and BR1000G entries. */
        .vid         = 0x051D,
        .pid         = 0x0002,
        .vendor_name = "APC",
        .model_hint  = "Back-UPS (PID 0002)",
        .decode_mode = DECODE_APC_BACKUPS,
        .quirks      = QUIRK_VENDOR_PAGE_REMAP | QUIRK_NEEDS_GET_REPORT,
        .known_good  = true,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
    {
        /* APC Smart-UPS C / Smart-UPS (PID 0x0003)
         * Confirmed: Smart-UPS C 1500 (issue #1, v15.14 log, Omar).
         * runtime arrives as undocumented interrupt-IN rid=0x0D (uint16 LE, seconds)
         * status arrives as undocumented interrupt-IN rid=0x07 (bit2=AC, bit1=discharging)
         * charge arrives as descriptor-declared Input rid=0x0C uid=0x0066
         * charging/discharging flags at rid=0x06 uid=0x008B/0x002C (descriptor Input)
         * These are NOT in the descriptor as Input — direct decode required.
         * QUIRK_NEEDS_GET_REPORT: rid=0x06 charging flags + rid=0x0E battery.voltage
         * via Feature reports (same as Back-UPS GET_REPORT path). */
        .vid         = 0x051D,
        .pid         = 0x0003,
        .vendor_name = "APC",
        .model_hint  = "Smart-UPS C / Smart-UPS (PID 0003)",
        .decode_mode = DECODE_APC_SMARTUPS,
        .quirks      = QUIRK_VENDOR_PAGE_REMAP | QUIRK_NEEDS_GET_REPORT,
        .known_good  = true,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
    {
        /* VID-only fallback: Smart-UPS and others
         * Using Safe APC defaults from NUT DDL. */
        .vid         = 0x051D,
        .pid         = 0,
        .vendor_name = "APC",
        .model_hint  = "Smart-UPS / other",
        .decode_mode = DECODE_STANDARD,
        .quirks      = QUIRK_VENDOR_PAGE_REMAP,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* ----------------------------------------------------------------
       Eaton / MGE / Powerware (VID 0x0463)
       Standard HID path — no known quirks.
    ---------------------------------------------------------------- */
    {
        /* Eaton/MGE/Powerware standard HID path.
         * NUT DDL: battery.voltage.nominal varies (12V small, 24V mid, 48V large)
         * Using 24V as safe mid-range default. runtime.low=120s from NUT DDL.
         * EU-targeted: input.voltage.nominal=230V. */
        .vid         = 0x0463,
        .pid         = 0,
        .vendor_name = "Eaton/MGE",
        .model_hint  = "3S/5E/5P/Ellipse/Evolution",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 230,
        .ups_type                   = "line-interactive",
    },

    /* ----------------------------------------------------------------
       Tripp Lite (VID 0x09AE)
       Some models need GET_REPORT for Feature values.
    ---------------------------------------------------------------- */
    {
        /* Tripp Lite SmartPro/OMNI — standard HID + GET_REPORT.
         * NUT DDL: battery.voltage.nominal=24V, runtime.low=120s.
         * US-targeted: input.voltage.nominal=120V. */
        .vid         = 0x09AE,
        .pid         = 0,
        .vendor_name = "Tripp Lite",
        .model_hint  = "OMNI/SMART/INTERNETOFFICE",
        .decode_mode = DECODE_STANDARD,
        .quirks      = QUIRK_NEEDS_GET_REPORT,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* ----------------------------------------------------------------
       Belkin (VID 0x050D)
    ---------------------------------------------------------------- */
    {
        .vid         = 0x050D,
        .pid         = 0,
        .vendor_name = "Belkin",
        .model_hint  = "F6H/F6C Series",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "standby",
    },

    /* ----------------------------------------------------------------
       Liebert / Vertiv (VID 0x10AF)
    ---------------------------------------------------------------- */
    {
        /* Liebert GXT4/PSI5 — typically double-conversion online.
         * NUT DDL: battery.voltage.nominal=24V-48V. Using 24V default. */
        .vid         = 0x10AF,
        .pid         = 0,
        .vendor_name = "Liebert",
        .model_hint  = "GXT4/PSI5",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "online",
    },

    /* ----------------------------------------------------------------
       Powercom (VID 0x0D9F)
    ---------------------------------------------------------------- */
    {
        .vid         = 0x0D9F,
        .pid         = 0,
        .vendor_name = "Powercom",
        .model_hint  = "Black Knight/Dragon/King Pro",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 12000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 230,
        .ups_type                   = "line-interactive",
    },

    /* ----------------------------------------------------------------
       HP (VID 0x03F0)
    ---------------------------------------------------------------- */
    {
        /* HP T-series — typically line-interactive, 24V battery.
         * NUT DDL values from T750/T1000 G3 confirmed entries. */
        .vid         = 0x03F0,
        .pid         = 0,
        .vendor_name = "HP",
        .model_hint  = "T750/T1000/T1500/T3000 G2/G3",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* ----------------------------------------------------------------
       Dell (VID 0x047C)
    ---------------------------------------------------------------- */
    {
        .vid         = 0x047C,
        .pid         = 0,
        .vendor_name = "Dell",
        .model_hint  = "H750E/H950E/H1000E/H1750E",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },

    /* ----------------------------------------------------------------
       SENTINEL — must be last.
       Generic fallback for unknown devices — conservative defaults.
    ---------------------------------------------------------------- */
    {
        .vid         = 0,
        .pid         = 0,
        .vendor_name = "Unknown",
        .model_hint  = "Generic HID UPS",
        .decode_mode = DECODE_STANDARD,
        .quirks      = 0,
        .known_good  = false,
        .battery_voltage_nominal_mv = 0,      /* unknown — do not serve */
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 0,      /* unknown — do not serve */
        .ups_type                   = NULL,
    },
};

/* ----------------------------------------------------------------------- */

const ups_device_entry_t *ups_device_db_lookup(uint16_t vid, uint16_t pid)
{
    const ups_device_entry_t *vid_only = NULL;

    for (size_t i = 0; i < sizeof(s_db) / sizeof(s_db[0]); i++) {
        const ups_device_entry_t *e = &s_db[i];

        /* Sentinel — generic fallback */
        if (e->vid == 0 && e->pid == 0) {
            return vid_only ? vid_only : e;
        }

        if (e->vid != vid) continue;

        if (e->pid == pid) return e;           /* exact match */
        if (e->pid == 0 && !vid_only) vid_only = e; /* VID-only, keep best */
    }

    /* Should never reach here due to sentinel, but be safe */
    return &s_db[(sizeof(s_db) / sizeof(s_db[0])) - 1];
}

void ups_device_db_log(const ups_device_entry_t *entry, uint16_t vid, uint16_t pid)
{
    if (!entry) return;

    if (entry->vid == 0) {
        ESP_LOGW(TAG, "VID:PID=%04X:%04X — UNKNOWN device. "
                 "Attempting generic HID standard path. "
                 "Add to ups_device_db.c if this device works.",
                 vid, pid);
        return;
    }

    if (entry->known_good) {
        ESP_LOGI(TAG, "VID:PID=%04X:%04X — %s %s [known-good, %s path, quirks=0x%04"PRIx32"]",
                 vid, pid,
                 entry->vendor_name,
                 entry->model_hint ? entry->model_hint : "",
                 entry->decode_mode == DECODE_CYBERPOWER ? "direct-decode" : "standard",
                 (uint32_t)entry->quirks);
    } else {
        ESP_LOGW(TAG, "VID:PID=%04X:%04X — %s %s [not confirmed, %s path, quirks=0x%04"PRIx32"] "
                 "— may work, feedback welcome",
                 vid, pid,
                 entry->vendor_name,
                 entry->model_hint ? entry->model_hint : "",
                 entry->decode_mode == DECODE_CYBERPOWER ? "direct-decode" : "standard",
                 (uint32_t)entry->quirks);
    }
}
