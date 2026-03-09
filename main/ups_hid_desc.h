/*============================================================================
 MODULE: ups_hid_desc (public API)

 PURPOSE
 Parse a USB HID Report Descriptor byte stream into a flat table of field
 descriptors.  Each entry maps one logical field (a single Usage within a
 report) to the information needed to extract and scale its value from a
 raw interrupt-IN or polled report.

 This replaces the old hardcoded APC report-ID approach and makes the
 parser work with ANY UPS that conforms to the USB HID Power Device
 Class specification (usage pages 0x84 Power Device, 0x85 Battery System).

 REVERT HISTORY
 R0  v15.0  Initial — replaces APC-only ups_hid_parser model-hint approach

 USAGE PAGES
   0x84  Power Device (Input, Output, Flow, PowerConverter ...)
   0x85  Battery System (Charging, RemainingCapacity, RunTimeToEmpty ...)
   0xFF84 / 0xFF85  APC vendor extensions (treated same as 0x84/0x85)

 UNIT_EXPONENT ENCODING (HID spec §6.2.2.7)
   Nibble  Meaning
   0x0      ×10^0   (raw = value)
   0x1      ×10^1
   ...
   0x7      ×10^7
   0x8      ×10^-8  (two's complement nibble)
   ...
   0xF      ×10^-1

   We store as a signed int8 after decoding the nibble.

 UNIT ENCODING (HID spec §6.2.2.6)
   We only care about Voltage (Volt), Current (Amp), Time (s), and None.
   We derive NUT variable units from the Usage, not from UNIT tags.

============================================================================*/
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of fields we track from one report descriptor.
 * Typical UPS descriptors have 30–120 fields; 256 is safe headroom. */
#define UPS_HID_MAX_FIELDS  256

/* Maximum number of distinct report IDs we track. */
#define UPS_HID_MAX_REPORTS 64

/*---------------------------------------------------------------------------
 Known USB HID Power Device usages we care about.
 Encoded as (usage_page << 16) | usage_id  for fast comparison.
 All standard usages are on page 0x84 or 0x85; APC may use 0xFF84/0xFF85
 but maps the same usage IDs so we mask the page to the low byte for
 comparison.
---------------------------------------------------------------------------*/

/* Usage page constants */
#define HID_PAGE_POWER_DEVICE   0x84u
#define HID_PAGE_BATTERY_SYSTEM 0x85u

/* Power Device page (0x84) usages */
#define HID_USAGE_PD_IDEVICEID              0x0001u  /* UPS */
#define HID_USAGE_PD_PRESENTSTATUS          0x0002u  /* PresentStatus collection */
#define HID_USAGE_PD_CHANGEDFSTATUS         0x0003u
#define HID_USAGE_PD_UPS                    0x0004u
#define HID_USAGE_PD_POWERCONVERTER         0x0016u
#define HID_USAGE_PD_POWERCONVERTERID       0x0017u
#define HID_USAGE_PD_INPUT                  0x001Au  /* Input collection */
#define HID_USAGE_PD_INPUTID                0x001Bu
#define HID_USAGE_PD_OUTPUT                 0x001Cu  /* Output collection */
#define HID_USAGE_PD_OUTPUTID               0x001Du
#define HID_USAGE_PD_FLOW                   0x001Eu
#define HID_USAGE_PD_FLOWID                 0x001Fu
#define HID_USAGE_PD_OUTLET                 0x0020u
#define HID_USAGE_PD_OUTLETID               0x0021u
#define HID_USAGE_PD_VOLTAGE                0x0030u  /* input.voltage / output.voltage */
#define HID_USAGE_PD_CURRENT                0x0031u
#define HID_USAGE_PD_FREQUENCY              0x0032u
#define HID_USAGE_PD_APPARENTPOWER          0x0033u
#define HID_USAGE_PD_ACTIVEPOWER            0x0034u
#define HID_USAGE_PD_PERCENTLOAD            0x0035u  /* ups.load */
#define HID_USAGE_PD_TEMPERATURE            0x0036u  /* ups.temperature */
#define HID_USAGE_PD_HUMIDITY               0x0037u
#define HID_USAGE_PD_CONFIGVOLTAGE          0x0040u
#define HID_USAGE_PD_CONFIGCURRENT          0x0041u
#define HID_USAGE_PD_CONFIGFREQUENCY        0x0042u
#define HID_USAGE_PD_CONFIGAPPARENTPOWER    0x0043u
#define HID_USAGE_PD_LOWVOLTAGETRANSFER     0x0053u
#define HID_USAGE_PD_HIGHVOLTAGETRANSFER    0x0054u
#define HID_USAGE_PD_DELAYBEFORESHUTOWN     0x0057u
#define HID_USAGE_PD_DELAYBEFORESTARTUP     0x0056u
#define HID_USAGE_PD_SWITCHONOFF            0x0061u
#define HID_USAGE_PD_SWITCHABLE             0x0062u
#define HID_USAGE_PD_USED                   0x0063u
#define HID_USAGE_PD_BOOST                  0x006Eu  /* AVR boost active */
#define HID_USAGE_PD_BUCK                   0x006Fu  /* AVR buck active */
#define HID_USAGE_PD_OVERLOAD               0x0065u
#define HID_USAGE_PD_OVERVOLTAGE            0x0066u
#define HID_USAGE_PD_ACPRESENT              0x00D0u  /* input utility present (some vendors) */

/* Battery System page (0x85) usages */
#define HID_USAGE_BS_SMBBATTERYMODESTATE    0x0001u
#define HID_USAGE_BS_BATTERYID              0x0013u
#define HID_USAGE_BS_RECHARGEABLE           0x0027u
#define HID_USAGE_BS_WARNCAPACITYLIMIT      0x002Bu
#define HID_USAGE_BS_CAPACITYMODE           0x002Du
#define HID_USAGE_BS_TERMINATECHARGE        0x0040u
#define HID_USAGE_BS_TERMINATEDISCHARGE     0x0041u
#define HID_USAGE_BS_BELOWREMCAPLIMIT       0x0042u  /* low battery flag */
#define HID_USAGE_BS_REMTIMELIMITEXPIRED    0x0043u
#define HID_USAGE_BS_CHARGING               0x0044u  /* charging flag */
#define HID_USAGE_BS_DISCHARGING            0x0045u  /* discharging flag */
#define HID_USAGE_BS_FULLYCHARGED           0x0046u
#define HID_USAGE_BS_FULLYDISCHARGED        0x0047u
#define HID_USAGE_BS_ABSOLUTESOC            0x0066u  /* AbsoluteStateOfCharge = battery.charge */
#define HID_USAGE_BS_RELATIVESOC            0x0065u  /* RelativeStateOfCharge */
#define HID_USAGE_BS_REMAININGCAPACITY      0x0066u  /* same as AbsoluteSOC on most devices */
#define HID_USAGE_BS_RUNTIMETOEMPTY         0x0068u  /* battery.runtime */
#define HID_USAGE_BS_AVERAGETIMETOEMPTY     0x0069u
#define HID_USAGE_BS_VOLTAGE                0x0083u  /* battery.voltage */
#define HID_USAGE_BS_DESIGNCAPACITY         0x0083u  /* DesignCapacity (same ID, context differs) */
#define HID_USAGE_BS_ACPRESENT              0x00D0u  /* ACPresent = input utility present */
#define HID_USAGE_BS_NEEDREPLACEMENT        0x004Bu  /* battery needs replacement */
#define HID_USAGE_BS_CYCLECOUNT             0x006Bu

/* Compose a 32-bit usage key: page in high 16, usage in low 16 */
#define HID_USAGE_KEY(page, uid)  (((uint32_t)(page) << 16) | (uint32_t)(uid))

/*---------------------------------------------------------------------------
 Field descriptor — one entry per logical data field found in the descriptor.
---------------------------------------------------------------------------*/
typedef struct {
    uint8_t   report_id;       /* Which report this field lives in (0 = no report ID) */
    uint8_t   report_type;     /* 0=Input, 1=Output, 2=Feature */
    uint16_t  bit_offset;      /* Bit offset within the report payload (after report ID byte) */
    uint8_t   bit_size;        /* Number of bits */
    uint8_t   usage_page;      /* Usage page (low byte; 0x84 or 0x85 for power device) */
    uint16_t  usage_id;        /* Usage within that page */
    int32_t   logical_min;     /* Logical minimum from descriptor */
    int32_t   logical_max;     /* Logical maximum from descriptor */
    int8_t    unit_exponent;   /* Decoded unit exponent (signed: e.g. -2 means ×10^-2) */
    bool      is_signed;       /* true if logical_min < 0 */
} hid_field_t;

/*---------------------------------------------------------------------------
 Report map — maps report_id → total payload byte length (excluding ID byte).
---------------------------------------------------------------------------*/
typedef struct {
    uint8_t  report_id;
    uint16_t input_bytes;    /* byte length of Input report payload */
    uint16_t output_bytes;
    uint16_t feature_bytes;
} hid_report_size_t;

/*---------------------------------------------------------------------------
 Parsed descriptor result — output of ups_hid_desc_parse().
---------------------------------------------------------------------------*/
typedef struct {
    hid_field_t       fields[UPS_HID_MAX_FIELDS];
    uint16_t          field_count;

    hid_report_size_t reports[UPS_HID_MAX_REPORTS];
    uint8_t           report_count;

    bool              has_report_ids;  /* false if all fields use implicit report ID 0 */
    bool              valid;
} hid_desc_t;

/*---------------------------------------------------------------------------
 API
---------------------------------------------------------------------------*/

/**
 * Parse a raw HID report descriptor byte array.
 *
 * @param desc_bytes  Pointer to the raw descriptor bytes
 * @param desc_len    Length in bytes
 * @param out         Output structure (must be zeroed by caller or use
 *                    ups_hid_desc_init() first)
 * @return            true on success, false if descriptor is malformed
 */
bool ups_hid_desc_parse(const uint8_t *desc_bytes, size_t desc_len, hid_desc_t *out);

/**
 * Zero-initialise a hid_desc_t before passing to ups_hid_desc_parse().
 */
void ups_hid_desc_init(hid_desc_t *desc);

/**
 * Find the first Input field with the given usage page and usage ID.
 * Returns NULL if not found.
 */
const hid_field_t *ups_hid_desc_find_input(const hid_desc_t *desc,
                                            uint8_t usage_page,
                                            uint16_t usage_id);

/**
 * Find ALL Input fields with the given usage page.
 * Writes up to max_out pointers into out_fields[].
 * Returns the count found.
 */
uint8_t ups_hid_desc_find_inputs_by_page(const hid_desc_t *desc,
                                          uint8_t usage_page,
                                          const hid_field_t **out_fields,
                                          uint8_t max_out);

/**
 * Extract a signed integer value from a raw report byte buffer.
 *
 * @param data      Report data buffer (NOT including the report-ID byte)
 * @param data_len  Length of data buffer in bytes
 * @param field     Field descriptor from ups_hid_desc_parse()
 * @param out_raw   Output: raw logical integer value
 * @return          true on success
 */
bool ups_hid_desc_extract_field(const uint8_t *data, size_t data_len,
                                 const hid_field_t *field,
                                 int32_t *out_raw);

/**
 * Apply unit exponent to scale a raw value to micro-units.
 * For example, if the descriptor says unit_exponent = -3 (milli-),
 * a raw value of 120 → 120 * 10^-3 → stored as 120000 µV? No —
 * we use millivolts throughout, so we normalise to milli-units.
 *
 * Specifically: returns raw * 10^(exponent + 3) as integer milli-units.
 * Works for voltage (V→mV), current (A→mA), time (s→ms is NOT used —
 * we keep runtime in seconds).
 *
 * @param raw            Raw logical value
 * @param unit_exponent  From field descriptor
 * @param out_milli      Output in milli-units (mV, mA, etc.)
 * @return               true if conversion was within int32 range
 */
bool ups_hid_desc_to_milli(int32_t raw, int8_t unit_exponent, int32_t *out_milli);

/**
 * Log all parsed fields to the ESP-IDF log system (INFO level).
 * Used during development / first boot with new device.
 */
void ups_hid_desc_dump(const hid_desc_t *desc);

#ifdef __cplusplus
}
#endif
