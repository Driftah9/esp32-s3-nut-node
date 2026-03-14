# usbhid-ups — Full NUT Hardware Compatibility & HID Field Reference
**NUT Source:** https://github.com/networkupstools/nut  
**HID Subdrivers reviewed:** `cps-hid.c`, `apc-hid.c`, `mge-hid.c`, `tripplite-hid.c`, `liebert-hid.c`  
**HID type definitions:** `drivers/hidtypes.h`  
**Updated:** 2026-03-09  
**Total usbhid-ups devices:** 338+ across 29 manufacturers  
**Driver:** `usbhid-ups` — USB HID Power Device Class (standard + vendor subdrivers)

---

## Architecture: How usbhid-ups Decodes Fields

NUT's `usbhid-ups` driver works in three phases:

1. **USB GET_DESCRIPTOR** — fetches the HID Report Descriptor from the device
2. **HID Path resolution** — each NUT variable has a "HID path" like  
   `UPS.PowerSummary.RemainingCapacity` which resolves via Usage Page/ID lookups  
   through a chain of nested HID Collections
3. **Report polling** — interrupt-IN or GetReport fetches are decoded using  
   the field locations from the parsed descriptor

Our `ups_hid_parser.c` replicates this: parse the descriptor once, build a field cache,
then decode each interrupt-IN report by report ID and field bit-offset.

---

## Usage ID Corrections (vs. previous doc)

The previous doc had **incorrect** usage ID mappings. Corrected from `hidtypes.h`:

| Usage | Hex | Previous (wrong) | Correct |
|-------|-----|-----------------|---------|
| Relative State of Charge | 0x85/0x64 | listed as 0x65 | **0x64** |
| Absolute State of Charge | 0x85/0x65 | listed as 0x66 | **0x65** |
| Remaining Capacity | 0x85/0x66 | labelled AbsoluteSOC | **0x66 = RemainingCapacity** |
| Full Charge Capacity | 0x85/0x67 | missing | **0x67** |
| Run Time To Empty | 0x85/0x68 | correct | 0x68 ✅ |
| Battery Voltage | 0x85/0x83 | listed as DesignCapacity also | 0x83 (context determines meaning) |
| AC Present | 0x85/0xD0 | correct | 0xD0 ✅ |

**Impact on our code:** `ups_hid_desc.h` uses wrong defines:
```c
// WRONG:
#define HID_USAGE_BS_ABSOLUTESOC  0x0066u  // This is actually REMAINING_CAPACITY
#define HID_USAGE_BS_RELATIVESOC  0x0065u  // This is actually ABSOLUTE_SOC

// CORRECT (from hidtypes.h):
#define HID_USAGE_BS_RELATIVESOC       0x0064u  // USAGE_BAT_RELATIVE_STATE_OF_CHARGE
#define HID_USAGE_BS_ABSOLUTESOC       0x0065u  // USAGE_BAT_ABSOLUTE_STATE_OF_CHARGE
#define HID_USAGE_BS_REMAININGCAP      0x0066u  // USAGE_BAT_REMAINING_CAPACITY  ← CyberPower uses this
#define HID_USAGE_BS_FULLCHARGECAP     0x0067u  // USAGE_BAT_FULL_CHARGE_CAPACITY
```

**The battery.charge decode currently works by accident** — CyberPower reports
`REMAINING_CAPACITY` (0x66) which our code labels `ABSOLUTESOC`. Fix the constant
names but keep matching 0x66.

---

## Complete HID Usage → NUT Variable Mapping

### Power Device Page (0x84)

| Usage ID | HID Name | NUT Variable | Notes |
|----------|----------|-------------|-------|
| 0x0001 | iName | ups.id | String index |
| 0x0004 | UPS | — | Collection type |
| 0x001A | Input | — | Input collection |
| 0x001C | Output | — | Output collection |
| 0x0024 | PowerSummary | — | Battery summary collection |
| 0x0030 | Voltage | input.voltage / output.voltage / battery.voltage | Context-dependent |
| 0x0031 | Current | input.current / output.current | |
| 0x0032 | Frequency | input.frequency / output.frequency | |
| 0x0033 | ApparentPower | ups.power | VA |
| 0x0034 | ActivePower | ups.realpower | W |
| 0x0035 | PercentLoad | ups.load | % |
| 0x0036 | Temperature | ups.temperature | Kelvin in HID, convert to °C |
| 0x0037 | Humidity | ups.humidity | |
| 0x0040 | ConfigVoltage | input.voltage.nominal / output.voltage.nominal / battery.voltage.nominal | |
| 0x0041 | ConfigCurrent | — | |
| 0x0042 | ConfigFrequency | input.frequency.nominal / output.frequency.nominal | |
| 0x0043 | ConfigApparentPower | ups.power.nominal | |
| 0x0044 | ConfigActivePower | ups.realpower.nominal | CPS: LogMax bug, needs fix |
| 0x0045 | ConfigPercentLoad | — | |
| 0x0053 | LowVoltageTransfer | input.transfer.low | RW |
| 0x0054 | HighVoltageTransfer | input.transfer.high | RW |
| 0x0055 | DelayBeforeReboot | ups.timer.reboot | |
| 0x0056 | DelayBeforeStartup | ups.timer.start / ups.delay.start | RW |
| 0x0057 | DelayBeforeShutdown | ups.timer.shutdown / ups.delay.shutdown | RW |
| 0x0058 | Test | ups.test.result | |
| 0x005A | AudibleAlarmControl | ups.beeper.status | |
| 0x0061 | Good | — | Status flag |
| 0x0062 | InternalFailure | — | alarm |
| 0x0063 | VoltageOutOfRange | — | alarm |
| 0x0064 | FrequencyOutOfRange | — | alarm |
| 0x0065 | Overload | — | ups.alarm |
| 0x0066 | OverCharged | — | alarm |
| 0x0067 | OverTemperature | — | alarm |
| 0x0068 | ShutdownRequested | — | alarm |
| 0x0069 | ShutdownImminent | — | alarm |
| 0x006B | SwitchOnOff | — | |
| 0x006E | Boost | — | AVR boost active flag |
| 0x006F | Buck | — | AVR buck active flag |
| 0x0070 | Initialized | — | |
| 0x0071 | Tested | — | |
| 0x00FD | iManufacturer | ups.mfr | String index |
| 0x00FE | iProduct | ups.model | String index |
| 0x00FF | iSerialNumber | ups.serial | String index |

### Battery System Page (0x85)

| Usage ID | HID Name | NUT Variable | Notes |
|----------|----------|-------------|-------|
| 0x0001 | SMBBatteryMode | — | SMBus compat |
| 0x0028 | ManufacturerAccess | — | |
| 0x0029 | RemainingCapacityLimit | battery.charge.low | RW; CyberPower uses this |
| 0x002A | RemainingTimeLimit | battery.runtime.low | RW |
| 0x002B | AtRate | — | |
| 0x002C | CapacityMode | — | CyberPower exposes this |
| 0x0040 | TerminateCharge | — | |
| 0x0041 | TerminateDischarge | — | |
| 0x0042 | BelowRemainingCapacityLimit | **low battery flag** → OB LB | Quick-poll |
| 0x0043 | RemainingTimeLimitExpired | time limit expired alarm | |
| 0x0044 | Charging | **charging flag** → OL CHRG | Quick-poll |
| 0x0045 | Discharging | **discharging flag** → OB DISCHRG | Quick-poll |
| 0x0046 | FullyCharged | fully charged flag | |
| 0x0047 | FullyDischarged | — | |
| 0x0048 | ConditioningFlag | — | |
| 0x004B | NeedReplacement | battery.alarm → replace | |
| 0x0060 | AtRateTimeToFull | — | |
| 0x0061 | AtRateTimeToEmpty | — | |
| 0x0062 | AverageCurrent | — | |
| 0x0063 | MaxError | — | |
| 0x0064 | RelativeStateOfCharge | battery.charge | % — some devices use this |
| 0x0065 | AbsoluteStateOfCharge | battery.charge | % — some devices use this |
| 0x0066 | RemainingCapacity | **battery.charge** | % — **CyberPower uses this** |
| 0x0067 | FullChargeCapacity | battery.status (as %) | CyberPower uses this for status |
| 0x0068 | RunTimeToEmpty | **battery.runtime** | seconds |
| 0x0069 | AverageTimeToEmpty | — | |
| 0x006A | AverageTimeToFull | — | |
| 0x006B | CycleCount | battery.cycle.count | |
| 0x0083 | DesignCapacity | — | CyberPower exposes this; also overloaded as Battery Voltage in some contexts |
| 0x0084 | SpecificationInfo | — | |
| 0x0085 | ManufacturerDate | battery.mfr.date | |
| 0x0086 | SerialNumber | battery.serial | |
| 0x0087 | iManufacturerName | battery.mfr | String index |
| 0x0088 | iDeviceName | battery.model | String index |
| 0x0089 | iDeviceChemistry | battery.type | String index — CyberPower uses this |
| 0x008A | ManufacturerData | — | |
| 0x008B | Rechargeable | — | CyberPower exposes this |
| 0x008C | WarningCapacityLimit | battery.charge.warning | CyberPower uses this |
| 0x008D | CapacityGranularity1 | — | CyberPower uses this |
| 0x008E | CapacityGranularity2 | — | CyberPower uses this |
| 0x008F | iOEMInformation | battery.mfr.date (alt) | String index — CyberPower uses this |
| 0x00C0 | InhibitCharge | — | |
| 0x00C1 | EnablePolling | — | |
| 0x00D0 | ACPresent | **input utility present → OL/OB** | Quick-poll |
| 0x00D1 | BatteryPresent | — | |
| 0x00D2 | PowerFail | — | |
| 0x00D3 | AlarmInhibited | — | |
| 0x00D8 | VoltageOutOfRange | — | alarm |
| 0x00DA | CurrentNotRegulated | — | alarm |
| 0x00DB | VoltageNotRegulated | — | alarm |

---

## CyberPower (VID=0764) Specific Details

**Confirmed from live boot log** — VID=0764 PID=0501, 607-byte descriptor, 14 fields:

### Your Device's Actual HID Report Map

| Field | rid | page | usage_id | Meaning | NUT Variable |
|-------|-----|------|----------|---------|-------------|
| 0 | 0x1D | 0x84 | 0x00FE | iProduct | ups.model |
| 1 | 0x03 | 0x84 | 0x00FD | iManufacturer | ups.mfr |
| 2 | 0x03 | 0x85 | 0x008F | iOEMInformation | battery.mfr.date (alt) |
| 3 | 0x04 | 0x85 | 0x0089 | iDeviceChemistry | battery.type |
| 4 | 0x06 | 0x85 | 0x008B | Rechargeable | — |
| 5 | 0x06 | 0x85 | 0x002C | CapacityMode | — |
| 6 | 0x07 | 0x85 | 0x0083 | DesignCapacity | — |
| 7 | 0x07 | 0x85 | 0x008D | CapacityGranularity1 | — |
| 8 | 0x07 | 0x85 | 0x008E | CapacityGranularity2 | — |
| 9 | 0x07 | 0x85 | 0x008C | WarningCapacityLimit | battery.charge.warning |
| 10 | 0x07 | 0x85 | 0x0029 | RemainingCapacityLimit | battery.charge.low |
| 11 | 0x07 | 0x85 | 0x0067 | FullChargeCapacity | battery.status (%) |
| 12 | 0x20 | 0x85 | 0x0066 | **RemainingCapacity** | **battery.charge** ✅ decoded |
| 13 | 0x20 | 0x85 | 0x0066 | RemainingCapacity (Output/Feature dup) | — |

### Undecoded Reports from Live Monitor

The device sends reports on IDs not in the 14-field descriptor table above.
These are likely in sections of the descriptor our parser is dropping (see bug below):

| Report ID | Raw bytes | Decoded value | Likely NUT variable |
|-----------|-----------|---------------|---------------------|
| 0x20 | `20 64 92 00` | field[12]=100 ✅ | battery.charge=100% |
| 0x21 | `21 6A C6` | 0x6A=106, 0xC6=198 | battery.runtime? (106s?) |
| 0x22 | `22 09` | 9 | some config/status byte |
| **0x23** | **`23 76 00 76 00`** | **0x76=118, 0x76=118** | **input.voltage=118V, output.voltage=118V** |
| 0x25 | `25 00` | 0 | unknown flag |
| 0x28 | `28 00` | 0 | unknown |
| 0x29 | `29 00` | 0 | unknown |
| 0x80 | `80 01` | 1 | ACPresent=1 (on utility) → OL |
| 0x82 | `82 2C 01` | 0x012C=300 | battery.runtime=300s? |
| 0x85 | `85 00` | 0 | status flag |
| 0x86 | `86 FF FF` | 0xFFFF | unset/invalid |
| 0x87 | `87 FF FF` | 0xFFFF | unset/invalid |
| 0x88 | `88 78 00` | 0x0078=120 | battery.voltage? (12.0V?) |

**Report 0x23 is almost certainly input/output voltage = 118V** — two 16-bit LE values.  
**Report 0x80 byte=01 is ACPresent** — matches `USAGE_BAT_AC_PRESENT` (0x85/0xD0).  
**Report 0x82 = 300 seconds** — likely `battery.runtime` (RunTimeToEmpty).

### CyberPower Descriptor Bug (from cps-hid.c)

NUT's `cps_fix_report_desc()` explicitly patches PID=0x0501 descriptors because:
- Output voltage `LogMax` is incorrectly set to the HighVoltageTransfer max
- Input voltage `LogMax` may also be wrong for EU models
- Fix: force `LogMin=0, LogMax=511` for both input and output voltage fields

Our parser needs to apply the same fix: if input/output voltage `logical_max` matches
the high-voltage-transfer `logical_max`, override both to `[0, 511]`.

### CyberPower Battery Scale Factor (from cps-hid.c)

Some CyberPower firmware versions (including PID=0x0501 `cps_battery_scale` flag) report
battery voltage **1.5× too high**. NUT detects this at runtime:

```c
if (batt_volt / batt_volt_nom > 1.4) {
    battery_scale = 2.0 / 3.0;  // scale down by 2/3
}
```

We should implement the same: after first reading `battery.voltage`, compare to
`battery.voltage.nominal` (from the descriptor config fields). If ratio > 1.4, apply 2/3 scale.

### CyberPower Frequency Scale Factor (from cps-hid.c)

Some CyberPower devices report frequency as `499` when they mean `49.9 Hz`
(i.e., unit_exponent is wrong). NUT detects this at runtime:
- If reported value is in range [45..65] → scale = 1.0
- If reported value is in range [450..650] → scale = 0.1

---

## Root Cause: Why 9 of 10 Fields Are MISSING

### Bug: Parser Only Captures 14 of ~50+ Expected Fields

The descriptor is 607 bytes and contains 13 report IDs. Our parser only captures 14 fields
across those 13 IDs — but the live monitor shows data coming in on report IDs like
0x23, 0x80, 0x82, 0x88 that have **no parsed field**. This means the parser is silently
dropping fields.

**Likely causes (in order of probability):**

1. **Non-power-device usages filtered out** — `ups_hid_desc.c` only records fields where
   `page == 0x84 || page == 0x85 || page == 0xFF84 || page == 0xFF85`. Any field on a
   different page (e.g., the `GenericDesktop` page 0x01, or vendor page 0xFF) is silently
   dropped. The CyberPower descriptor likely wraps voltage/runtime in a
   `PowerSummary` collection that uses page 0x84 for the collection but the inner fields
   use the same page — this should be fine. More likely: some fields have usage_page=0
   or an unexpected vendor page.

2. **Usage range expansion overflow** — if a CPS report has `UsageMinimum/UsageMaximum`
   spanning many values, `MAX_USAGE_QUEUE=32` may truncate the list, causing some
   fields to be silently skipped.

3. **Collection reset bug** — `ups_hid_desc.c` resets local state on `ITEM_COLLECTION`
   (0xA0), which is correct. But if a `Usage` tag appears after a `Collection` open but
   before the first `Input/Output/Feature` main item, it may be consumed or dropped.

**Diagnostic action needed:** Enable full descriptor hex dump and compare against the
NUT `lsusb -v` output for PID=0x0501 (known in NUT issue tracker #1512).

---

## Fields Our Parser Must Decode (Priority Order)

These are the fields `cps_hid2nut[]` in NUT's `cps-hid.c` tells us CyberPower devices expose.
Marked with current decode status:

### Battery / Power Summary

| NUT Variable | HID Path | Page/Usage | Decode Status |
|-------------|----------|-----------|---------------|
| battery.charge | UPS.PowerSummary.RemainingCapacity | 0x85/0x66 | ✅ Working |
| battery.charge.low | UPS.PowerSummary.RemainingCapacityLimit | 0x85/0x29 | ❌ Field found but not decoded |
| battery.charge.warning | UPS.PowerSummary.WarningCapacityLimit | 0x85/0x008C | ❌ Field found but not decoded |
| battery.runtime | UPS.PowerSummary.RunTimeToEmpty | 0x85/0x68 | ❌ MISSING — field not found |
| battery.runtime.low | UPS.PowerSummary.RemainingTimeLimit | 0x85/0x2A | ❌ MISSING |
| battery.voltage | UPS.PowerSummary.Voltage | 0x84/0x30 (in PowerSummary) | ❌ MISSING |
| battery.voltage.nominal | UPS.PowerSummary.ConfigVoltage | 0x84/0x40 (in PowerSummary) | ❌ MISSING |
| battery.status | UPS.PowerSummary.FullChargeCapacity | 0x85/0x67 | ❌ Field found (rid=0x07) but not decoded |
| battery.type | UPS.PowerSummary.iDeviceChemistry | 0x85/0x89 | ❌ String index, needs GET_DESCRIPTOR |
| battery.mfr.date | UPS.PowerSummary.iOEMInformation | 0x85/0x8F | ❌ String index |

### UPS Status Flags (PowerSummary.PresentStatus collection)

| NUT Variable | HID Path | Page/Usage | Decode Status |
|-------------|----------|-----------|---------------|
| ACPresent → OL | UPS.PowerSummary.PresentStatus.ACPresent | 0x85/0xD0 | ❌ MISSING — field not found |
| Charging → CHRG | UPS.PowerSummary.PresentStatus.Charging | 0x85/0x44 | ❌ MISSING |
| Discharging → DISCHRG | UPS.PowerSummary.PresentStatus.Discharging | 0x85/0x45 | ❌ MISSING |
| LowBatt → LB | UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit | 0x85/0x42 | ❌ MISSING |
| FullyCharged | UPS.PowerSummary.PresentStatus.FullyCharged | 0x85/0x46 | ❌ MISSING |
| TimeLimitExpired | UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired | 0x85/0x43 | ❌ MISSING |
| NeedReplacement | UPS.PowerSummary.PresentStatus.NeedReplacement | 0x85/0x4B | ❌ MISSING |

### UPS Output

| NUT Variable | HID Path | Page/Usage | Decode Status |
|-------------|----------|-----------|---------------|
| ups.load | UPS.Output.PercentLoad | 0x84/0x35 | ❌ MISSING |
| ups.power | UPS.Output.ApparentPower | 0x84/0x33 | ❌ MISSING |
| ups.realpower | UPS.Output.ActivePower | 0x84/0x34 | ❌ MISSING |
| ups.power.nominal | UPS.Output.ConfigApparentPower | 0x84/0x43 | ❌ MISSING |
| ups.temperature | UPS.PowerSummary.Temperature | 0x84/0x36 | ❌ MISSING |
| ups.beeper.status | UPS.PowerSummary.AudibleAlarmControl | 0x84/0x5A | ❌ MISSING |
| output.voltage | UPS.Output.Voltage | 0x84/0x30 (in Output) | ❌ MISSING |
| output.voltage.nominal | UPS.Output.ConfigVoltage | 0x84/0x40 (in Output) | ❌ MISSING |
| output.frequency | UPS.Output.Frequency | 0x84/0x32 (in Output) | ❌ MISSING |

### UPS Input

| NUT Variable | HID Path | Page/Usage | Decode Status |
|-------------|----------|-----------|---------------|
| input.voltage | UPS.Input.Voltage | 0x84/0x30 (in Input) | ❌ MISSING |
| input.voltage.nominal | UPS.Input.ConfigVoltage | 0x84/0x40 (in Input) | ❌ MISSING |
| input.frequency | UPS.Input.Frequency | 0x84/0x32 (in Input) | ❌ MISSING |
| input.transfer.low | UPS.Input.LowVoltageTransfer | 0x84/0x53 | ❌ MISSING |
| input.transfer.high | UPS.Input.HighVoltageTransfer | 0x84/0x54 | ❌ MISSING |

### AVR Flags (Boost/Buck — automatic voltage regulation)

| NUT Variable | HID Path | Page/Usage | Decode Status |
|-------------|----------|-----------|---------------|
| ups.alarm: Boost | UPS.Output.Boost | 0x84/0x6E | ❌ MISSING |
| ups.alarm: Overload | UPS.Output.Overload | 0x84/0x65 | ❌ MISSING |

---

## Recommended Code Changes

### 1. Fix usage ID constants in ups_hid_desc.h

```c
// Replace:
#define HID_USAGE_BS_ABSOLUTESOC   0x0066u   // WRONG
#define HID_USAGE_BS_RELATIVESOC   0x0065u   // WRONG

// With:
#define HID_USAGE_BS_RELATIVESOC        0x0064u  // USAGE_BAT_RELATIVE_STATE_OF_CHARGE
#define HID_USAGE_BS_ABSOLUTESOC        0x0065u  // USAGE_BAT_ABSOLUTE_STATE_OF_CHARGE
#define HID_USAGE_BS_REMAININGCAP       0x0066u  // USAGE_BAT_REMAINING_CAPACITY (CyberPower)
#define HID_USAGE_BS_FULLCHARGECAP      0x0067u  // USAGE_BAT_FULL_CHARGE_CAPACITY
#define HID_USAGE_BS_AVGTIME_TOEMPTY    0x0069u  // USAGE_BAT_AVERAGE_TIME_TO_EMPTY
#define HID_USAGE_BS_AVGTIME_TOFULL     0x006Au  // USAGE_BAT_AVERAGE_TIME_TO_FULL
#define HID_USAGE_BS_CYCLECOUNT         0x006Bu  // USAGE_BAT_CYCLE_COUNT
#define HID_USAGE_BS_WARNLIMIT          0x008Cu  // USAGE_BAT_WARNING_CAPACITY_LIMIT
#define HID_USAGE_BS_REMCAPLIMIT        0x0029u  // USAGE_BAT_REMAINING_CAPACITY_LIMIT
#define HID_USAGE_BS_REMTIMELIMIT       0x002Au  // USAGE_BAT_REMAINING_TIME_LIMIT
#define HID_USAGE_BS_BATTERY_PRESENT    0x00D1u  // USAGE_BAT_BATTERY_PRESENT
#define HID_USAGE_BS_POWER_FAIL         0x00D2u  // USAGE_BAT_POWER_FAIL

// Add missing Power Device page usages:
#define HID_USAGE_PD_ACTIVE_POWER       0x0034u  // ups.realpower
#define HID_USAGE_PD_APPARENT_POWER     0x0033u  // ups.power
#define HID_USAGE_PD_CONFIG_VOLTAGE     0x0040u  // nominal voltage
#define HID_USAGE_PD_CONFIG_FREQUENCY   0x0042u  // nominal frequency
#define HID_USAGE_PD_CONFIG_APPARENT    0x0043u  // nominal VA
#define HID_USAGE_PD_CONFIG_ACTIVE      0x0044u  // nominal W
#define HID_USAGE_PD_LOW_V_TRANSFER     0x0053u  // input.transfer.low
#define HID_USAGE_PD_HIGH_V_TRANSFER    0x0054u  // input.transfer.high
#define HID_USAGE_PD_DELAY_STARTUP      0x0056u  // ups.delay.start
#define HID_USAGE_PD_DELAY_SHUTDOWN     0x0057u  // ups.delay.shutdown
#define HID_USAGE_PD_TEST               0x0058u  // ups.test
#define HID_USAGE_PD_AUDIBLE_ALARM      0x005Au  // ups.beeper.status
#define HID_USAGE_PD_OVERLOAD           0x0065u  // overload alarm
#define HID_USAGE_PD_OVER_CHARGED       0x0066u  // over-charged alarm
#define HID_USAGE_PD_OVER_TEMPERATURE   0x0067u  // over-temp alarm
#define HID_USAGE_PD_BOOST              0x006Eu  // AVR boost active
#define HID_USAGE_PD_BUCK               0x006Fu  // AVR buck active
```

### 2. Expand hid_field_cache_t in ups_hid_parser.c

Add these missing fields to the cache struct:

```c
typedef struct {
    // ... existing fields ...
    const hid_field_t *battery_remaining_cap;   // 0x85/0x66 (what CPS calls RemainingCapacity)
    const hid_field_t *battery_full_cap;         // 0x85/0x67 FullChargeCapacity
    const hid_field_t *battery_runtime;          // 0x85/0x68
    const hid_field_t *battery_charge_low;       // 0x85/0x29
    const hid_field_t *battery_charge_warning;   // 0x85/0x008C
    const hid_field_t *battery_voltage_nominal;  // 0x84/0x40 in PowerSummary context
    const hid_field_t *input_voltage;            // 0x84/0x30 (1st Voltage in Input collection)
    const hid_field_t *output_voltage;           // 0x84/0x30 (2nd Voltage)
    const hid_field_t *input_frequency;          // 0x84/0x32
    const hid_field_t *output_frequency;         // 0x84/0x32
    const hid_field_t *ups_load;                 // 0x84/0x35
    const hid_field_t *ups_power;                // 0x84/0x33
    const hid_field_t *ups_realpower;            // 0x84/0x34
    const hid_field_t *ups_temperature;          // 0x84/0x36
    const hid_field_t *input_transfer_low;       // 0x84/0x53
    const hid_field_t *input_transfer_high;      // 0x84/0x54
    const hid_field_t *boost_flag;               // 0x84/0x6E
    const hid_field_t *buck_flag;                // 0x84/0x6F
    const hid_field_t *overload_flag;            // 0x84/0x65
    const hid_field_t *timelimit_expired;        // 0x85/0x43
    const hid_field_t *battery_present;          // 0x85/0xD1
    // ... existing valid bool ...
} hid_field_cache_t;
```

### 3. ups_state_update_t needs new fields

```c
// Add to ups_state_update_t in ups_state.h:
bool     output_voltage_valid;
uint32_t output_voltage_mv;
bool     input_frequency_valid;
float    input_frequency_hz;
bool     output_frequency_valid;
float    output_frequency_hz;
bool     ups_power_valid;
uint32_t ups_power_va;
bool     ups_realpower_valid;
uint32_t ups_realpower_w;
bool     ups_temperature_valid;
float    ups_temperature_c;
bool     input_transfer_low_valid;
uint32_t input_transfer_low_mv;
bool     input_transfer_high_valid;
uint32_t input_transfer_high_mv;
bool     boost_active;
bool     buck_active;
bool     overload;
bool     need_replacement;       // (already in ups_flags, make explicit)
bool     time_limit_expired;
uint8_t  battery_charge_low;     // threshold %
uint8_t  battery_charge_warning; // threshold %
uint32_t battery_voltage_nominal_mv;
```

### 4. NUT variable publishing in nut_server.c

NUT clients expect these variable names. Ensure `nut_server.c` publishes:
```
battery.charge          ← already working
battery.runtime         ← add
battery.voltage         ← add
battery.voltage.nominal ← add
battery.charge.low      ← add
battery.charge.warning  ← add
input.voltage           ← add
input.frequency         ← add (with CPS freq scale fix)
output.voltage          ← add
output.frequency        ← add (with CPS freq scale fix)
ups.load                ← add
ups.power               ← add (if available)
ups.realpower           ← add (if available)
ups.temperature         ← add (if available)
ups.status              ← already working (OL/OB/CHRG/DISCHRG/LB)
ups.model               ← already working
ups.mfr                 ← already working
ups.firmware            ← add (CPS uses vendor usage 0xff0100d0)
input.transfer.low      ← add
input.transfer.high     ← add
```

### 5. CyberPower-specific descriptor fixes

Apply at parse time when VID=0764 and PID=0501:
- If `logical_max` of Input Voltage field == `logical_max` of HighVoltageTransfer: override both to `[0, 511]`
- Apply battery scale factor 2/3 if `battery.voltage / battery.voltage.nominal > 1.4`
- Apply frequency scale factor 0.1 if frequency value > 100 (clearly wrong order of magnitude)

---

## Manufacturer VID Reference

| Manufacturer | VID |
|-------------|-----|
| APC | 0x051D |
| CyberPower (CPS) | 0x0764 |
| CyberPower (ST Micro OEM / Cyber Energy) | 0x0483 |
| Eaton / MGE | 0x0463 |
| Tripp Lite | 0x09AE |
| Belkin | 0x050D |
| Liebert | 0x10AF |
| Powercom | 0x0D9F |
| HP | 0x03F0 |
| Dell | 0x047C |

---

## All Manufacturers Supporting usbhid-ups

Support levels: 1=Not tested / 2=Partial / 3=Good / 4=Complete / 5=Excellent

### AEG Power Solutions
| Model | Support |
|-------|---------|
| PROTECT NAS | 3 |
| PROTECT B | 3 |

### APC (VID: 0x051D)
| Model | Support |
|-------|---------|
| APC AP9584 Serial-to-USB kit | 3 |
| Back-UPS Pro USB | 3 |
| Back-UPS BK650M2-CH | 3 |
| Back-UPS (USB) | 3 |
| Back-UPS CS USB | 3 |
| Back-UPS RS USB | 3 |
| Back-UPS LS USB | 3 |
| Back-UPS ES/CyberFort 350 | 3 |
| Back-UPS ES 850G2 | 3 |
| Back-UPS BF500 | 3 |
| BACK-UPS XS LCD | 3 |
| Back-UPS XS 1000M | 3 |
| Back-UPS BK****M2-CH Series | 3 |
| Back-UPS BX****MI Series | 3 |
| Back-UPS BVK****M2 Series | 3 |
| SMC2200BI-BR | 3 |
| Smart-UPS (USB) | 3 |
| Smart-UPS 750 (SMT750I) | 3 |
| Smart-UPS 1500 (SMT1500I) | 3 |
| Smart-UPS X 750 (SMX750I) | 3 |
| Smart-UPS X 1500 (SMX1500I) | 3 |
| CS500 | 3 |

### Belkin (VID: 0x050D)
| Model | Support |
|-------|---------|
| F6H375-USB | 3 |
| Office Series F6C550-AVR | 3 |
| Regulator PRO-USB | 3 |
| Small Enterprise F6C1500-TW-RK | 3 |
| Universal UPS F6C100-UNV | 3 |
| Universal UPS F6C120-UNV | 3 |
| Universal UPS F6C800-UNV | 3 |
| Universal UPS F6C1100-UNV | 3 |
| Universal UPS F6C1200-UNV | 3 |

### Cyber Energy (ST Micro VID: 0x0483 PID: 0xa430)
| Model | Support |
|-------|---------|
| Models with USB | 3 |

### Cyber Power Systems / CyberPower (VID: 0x0764)
Your SX550G is PID 0x0501 (same as CP1200AVR, CP825AVR-G, CP1000AVRLCD, CP1000PFCLCD, CP1500C, CP550HG, etc.)
| Model | VID:PID | Support |
|-------|---------|---------|
| 900AVR/BC900D | 0764:0005 | 3 |
| CP1200AVR, CP825AVR-G, CP1000AVRLCD, CP1500C, CP550HG etc. | **0764:0501** | 3 |
| OR2200LCDRM2U, OR700LCDRM1U, PR6000LCDRTXL5U, CP1350EPFCLCD | 0764:0601 | 3 |
| AE550 | 3 |
| BL1250U | 3 |
| BR1000ELCD | 3 |
| CP1350AVRLCD | 3 |
| CP1500AVRLCD | 3 |
| CP850PFCLCD | 3 |
| CP1350PFCLCD | 3 |
| CP1350EPFCLCD | 3 |
| CP1500PFCLCD | 3 |
| CPJ500 | 3 |
| CP900AVR | 3 |
| CPS685AVR | 3 |
| CPS800AVR | 3 |
| Value 1500ELCD-RU | 3 |
| Value 400E | 3 |
| Value 600E | 3 |
| Value 800E | 3 |
| VP1200ELCD | 3 |
| CP1000PFCLCD | 3 |
| CP1500EPFCLCD | 3 |
| CP825AVR-G / LE825G | 3 |
| EC350G | 3 |
| EC750G | 3 |
| EC850LCD | 3 |
| OR1500ERM1U | 3 |
| OR2200LCDRM2U | 3 |
| OR700LCDRM1U | 3 |
| OR500LCDRM1U | 3 |
| RT650EI | 3 |
| UT2200E | 3 |
| PR1500RT2U | 3 |
| PR6000LCDRTXL5U | 3 |

### Dell (VID: 0x047C)
| Model | Support |
|-------|---------|
| H750E (USB) | 3 |
| H950E (USB) | 3 |
| H1000E (USB) | 3 |
| H1750E (USB) | 3 |

### Delta
| Model | Support |
|-------|---------|
| Amplon RT Series (USB) | 3 |
| Amplon N Series (USB) | 3 |

### Dynex
| Model | Support |
|-------|---------|
| DX-800U (USB) | 3 |

### EVER
| Model | Support |
|-------|---------|
| Sinline RT Series (USB) | 3 |
| Sinline XL Series (USB) | 3 |
| ECO Pro Series (USB) | 3 |

### Eaton / Powerware / MGE (VID: 0x0463)
| Model | Support |
|-------|---------|
| 3S (USB) | 3 |
| 5E (USB) | 3 |
| 5P (USB) | 3 |
| 5PX (USB) | 3 |
| 5SC (USB) | 3 |
| 5SX (USB) | 3 |
| 9E (USB) | 3 |
| 9PX (USB) | 3 |
| Ellipse ECO (USB) | 3 |
| Ellipse MAX (USB) | 3 |
| Evolution 650/850/1150/1550 (USB) | 3 |
| MGE Ellipse Premium (USB) | 3 |
| Powerware 5110/5115/5125/5130 (USB) | 3 |
| Powerware 9125/9130/9140/9155/9170/9355 (USB) | 3 |
| Comet EX RT (USB) | 3 |
| Galaxy 3000/5000 (USB) | 3 |
| Nova AVR (USB) | 3 |
| Pulsar EX/EXtreme/M/MX (USB) | 3 |

### Ecoflow
| Model | Support |
|-------|---------|
| Delta 3 Plus (USB) | 3 |

### Geek Squad
| Model | Support |
|-------|---------|
| GS1285U (USB) | 3 |

### GoldenMate
| Model | Support |
|-------|---------|
| UPS 1000VA Pro (USB) | 3 |

### HP (VID: 0x03F0)
| Model | Support |
|-------|---------|
| T750 G2 (USB) | 3 |
| T1000 G3 (USB) | 3 |
| T1500 G3 (USB) | 3 |
| T3000 G3 (USB) | 3 |

### IBM
| Model | Support |
|-------|---------|
| Various (USB port) | 3 |

### Ippon
| Model | Support |
|-------|---------|
| Back Power Pro (USB) | 3 |
| Smart Power Pro (USB) | 3 |

### Legrand
| Model | Support |
|-------|---------|
| KEOR SP (USB) | 3 |

### Liebert (VID: 0x10AF)
| Model | Support |
|-------|---------|
| GXT4 (USB) | 3 |
| PSI5 (USB) | 3 |

### MasterPower
| Model | Support |
|-------|---------|
| MF-UPS650VA (USB) | 3 |

### MGE Office Protection Systems
| Model | Support |
|-------|---------|
| Protection Center 500/675 VA (USB) | 3 |

### Minibox
| Model | Support |
|-------|---------|
| openUPS Intelligent UPS (USB port) | 3 |

### PowerWalker
| Model | Support |
|-------|---------|
| VI 650/850/1000/1500 SE (USB) | 3 |

### Powercom (VID: 0x0D9F)
| Model | Support |
|-------|---------|
| Black Knight Pro (USB) | 3 |
| Dragon (USB) | 3 |
| Imperial (USB) | 3 |
| King Pro (USB) | 3 |
| Raptor (USB) | 3 |
| Smart King / Smart King Pro (USB) | 3 |
| WOW (USB) | 3 |

### Powervar
| Model | Support |
|-------|---------|
| ABCE (USB) | 3 |
| ABCEG (USB) | 3 |

### Rocketfish
| Model | Support |
|-------|---------|
| RF-1000VA / RF-1025VA (USB) | 3 |

### Salicru
| Model | Support |
|-------|---------|
| SPS One Series (USB) | 3 |
| SPS Xtreme Series (USB) | 3 |

### Syndome
| Model | Support |
|-------|---------|
| TITAN Series (USB) | 3 |

### Tripp Lite (VID: 0x09AE)
| Model | Support |
|-------|---------|
| INTERNETOFFICE700 | 3 |
| OMNI650/900/1000/1500 LCD | 3 |
| SMART500RT1U | 3 |
| SMART700USB | 3 |
| SMART1000/1500 LCD | 3 |
| SMART2200RMXL2U | 3 |
| SmartPro 1500LCD | 3 |

### iDowell
| Model | Support |
|-------|---------|
| iBox UPS (USB) | 3 |

---

## Known Descriptor Quirks by Vendor

| Vendor | PID | Quirk | Fix |
|--------|-----|-------|-----|
| CyberPower | 0x0501 | Output voltage LogMax = HighVoltageTransfer LogMax (wrong) | Force [0, 511] |
| CyberPower | 0x0501 | Input voltage LogMax wrong for EU models | Force [0, 511] |
| CyberPower | 0x0501 | Battery voltage reported 1.5× too high on some firmware | Scale × 2/3 if ratio > 1.4 |
| CyberPower | 0x0501, 0x0601 | Frequency reported as e.g. 499 instead of 49.9 | Scale × 0.1 if value > 100 |
| CyberPower | 0x0601 | ConfigActivePower LogMax too small, clips power reading | Force LogMax = 2048 |
| APC | many | Uses vendor HID page 0xFF84/0xFF85 (normalize to 0x84/0x85) | Already handled |
| Tripp Lite | many | Uses feature reports for some values instead of interrupt-IN | Need GET_REPORT |

---

*Source: NUT drivers/cps-hid.c, drivers/hidtypes.h, drivers/usbhid-ups.c — March 2026*  
*Live data from CyberPower VID=0764 PID=0501 boot log — esp32-s3-nut-node v15.1*
