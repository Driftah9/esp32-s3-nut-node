# ESP32-S3 APC UPS USB HID → NUT Node

## Master Engineering Log (Source of Truth)

Purpose: This document is the **authoritative engineering record** for
the ESP32-S3 UPS node project. It tracks architecture, milestones,
verified behaviors, and the **exact working firmware** that corresponds
to each milestone.

This document is designed so that: • Any engineer can reproduce the
build\
• Firmware evolution is clearly tracked\
• Stable firmware versions are never lost\
• Conversations with engineering assistants can resume with full
context\
• USB behavior and validation history can be expanded by others over
time

  --------------------------
  FIRMWARE EVOLUTION TABLE
  --------------------------

  --------------------------------------------------------------------------------
  Version   Date         Milestone        Key Achievements          Status
  --------- ------------ ---------------- ------------------------- --------------
  v1        Initial      Architecture     System architecture       Historical
                         planning         defined                   

  v2        Initial      Environment      ESP-IDF configured        Historical
                         setup                                      

  v3        ---          Milestone 1      USB enumeration (UPS      Stable
                                          VID:PID detected)         

  v4        ---          Milestone 2      HID interrupt reading     Stable
                                          (raw reports streaming)   

  v5        ---          Milestone 3      Descriptor parsing        Stable
                                          (report desc length       
                                          discovered)               

  v6        ---          Milestone 4      Early decode (core report Stable
                                          IDs started)              

  v7        2026-03-05   Milestone 4      Descriptor dump path      Experimental
                         refinement       (best-effort /            
                                          experimental)             

  v8        2026-03-05   Milestone 5      UPS state model +         CURRENT
                                          NUT-style status logs     BASELINE

  v9        Planned      Milestone 6      Extended HID decode       Planned
                                          (0x06/0x14/0x21)          

  v10       Planned      Milestone 7      HTTP API (LAN status      Planned
                                          endpoint)                 

  v11       Planned      Milestone 8      NUT TCP server (minimal   Planned
                                          NUT subset)               

  v12       Planned      Milestone 9      Web UI (configuration     Planned
                                          interface)                
  --------------------------------------------------------------------------------

  ------------------
  PROJECT OVERVIEW
  ------------------

Goal: Build ESP32-S3 nodes that connect to APC UPS units via USB HID and
expose UPS telemetry to the LAN using a NUT-compatible protocol and web
interface.

Target Architecture:

UPS (RJ50 HID) ↓ ESP32-S3 USB OTG Host ↓ UPS Telemetry Decode ↓ Local
State Model ↓ LAN Services • HTTP Status API • NUT Protocol TCP Server
(3493) ↓ Central NUT Aggregator Orange Pi Zero 2W

LAN Range: 10.0.0.0/16

Central Aggregator: Orange Pi Zero 2W\
IP: 10.0.0.6

  -------------------
  HARDWARE BASELINE
  -------------------

Board: ESP32-S3 development board

Resources: Flash: 16MB\
PSRAM: 8MB

USB Ports: 1) UART / programming\
2) Native OTG

OTG VBUS: Powered via solder bridge from power USB port

UPS Connection Chain:

UPS RJ50\
→ APC RJ50-to-USB cable\
→ USB-A → USB-C adapter\
→ ESP32 OTG

  ----------------------
  SOFTWARE ENVIRONMENT
  ----------------------

ESP-IDF Version: 5.3.1

Build Commands:

idf.py set-target esp32s3\
idf.py build\
idf.py flash\
idf.py monitor

  ---------------------
  USB DEVICE BASELINE
  ---------------------

Target UPS: Vendor: APC\
VID:PID: 051d:0002

USB Parameters:

Speed: FULL\
HID Interface: 0\
Interrupt Endpoint: 0x81\
Endpoint MPS: 8\
Polling Interval: \~100ms

HID Report Descriptor Length: 1049 bytes

Observed Report IDs (from INT-IN stream):

06\
0C\
13\
14\
16\
21

  ---------------------------
  CURRENT VERIFIED BASELINE
  ---------------------------

Firmware Version: v8

Confirmed Behaviors:

• USB host installs successfully\
• APC UPS enumerates reliably\
• HID interface 0 claimed\
• Interrupt IN endpoint streaming works\
• Reports logged only when changed\
• Report decoding implemented:\
- 0x0C battery percent + runtime\
- 0x13 AC present\
- 0x16 status flags\
• State model maintained in firmware\
• Attach / detach handled cleanly

  ------------------------
  KNOWN ISSUES (TRACKED)
  ------------------------

  -----------------------------------------------------------------------------------------
  ISSUE-ID    First Seen     Status     Summary                  Notes / Workaround
  ----------- -------------- ---------- ------------------------ --------------------------
  KI-001      2026-03-05     Open       Control                  Descriptor dump disabled
                                        GET_DESCRIPTOR(report)   by default; not required
                                        can return               for INT-IN decode.
                                        ESP_ERR_INVALID_ARG      

  KI-002      2026-03-05     Observed   Report descriptor dump   Treat as dump-path edge
                                        length mismatch observed case; avoid using dump
                                        once (1057 vs expected   until fixed.
                                        1049)                    
  -----------------------------------------------------------------------------------------

Add new known issues above. Do not delete old issues---update status and
notes.

Status values: • Open • Mitigated • Fixed • Won't Fix • Needs Repro

  ---------------------------------------------
  USB BEHAVIOR REFERENCE (EXPECTED SEQUENCES)
  ---------------------------------------------

This section documents the **expected USB behavior** for the known-good
baseline. If behavior deviates, treat it as a regression until proven
otherwise.

## A) Expected Attach (Plug-in) Sequence

1)  USB host installed:
    -   "usb_host_install OK"
2)  Client registered:
    -   "usb_host_client_register OK"
3)  Device attached event:
    -   "NEW_DEV received: addr=X"
4)  Device opened and identified:
    -   "Connected VID:PID=051d:0002 ..."
5)  Config parsed:
    -   "Active config wTotalLength=..."
    -   "HID interface intf=0 alt=0 ..."
    -   "HID Report Descriptor length = 1049 bytes ... (from HID
        descriptor 0x21)"
    -   "Interrupt IN EP=0x81 MPS=8"
6)  Interface claimed:
    -   "Interface claimed"
7)  INT-IN reader started:
    -   "Starting interrupt IN reader: EP=0x81 MPS=8 (change-only)"
8)  First reports arrive (change-only):
    -   "HID IN changed (...) ..."
    -   Decodes for 0x0C / 0x13 / 0x16 show up as those frames arrive
9)  Milestone state reset on attach:
    -   "Milestone 5: state reset on attach."

## B) Expected Detach (Unplug) Sequence

1)  Device gone event:
    -   "DEV_GONE received"
2)  In-flight transfer may complete with error status; implementation
    must avoid resubmit loop
3)  Interface released and device closed
4)  State cleared:
    -   "Milestone 5: state cleared on detach."

## C) Expected Report Cadence / Patterns (Baseline Observations)

• Endpoint: 0x81, MPS 8, typical interval \~100ms\
• Reports repeat frequently but are logged only when changed\
• Common RIDs seen in a steady-state loop: - 0x06 (often seen as "06 00
00 08") - 0x0C (battery % + runtime seconds, e.g. "0C 62 D0 89") - 0x13
(AC present, "13 01" on-line or "13 00" on-battery) - 0x14 (often "14 00
00") - 0x16 (flags, 5 bytes total, e.g. "16 0D 00 00 00") - 0x21 (often
"21 06")

## D) Expected AC Pull / Restore Telemetry (Known-good Indicators)

During a deliberate AC pull test (\~20s typical): • 0x13: AC_present
changes 1 → 0\
• 0x16: flags change (example observed: 0x0000000C → 0x0000000A)\
• 0x0C: battery may drop shortly after (e.g., 100% → 99%) and runtime
updates

During AC restore: • 0x13: AC_present changes 0 → 1\
• 0x16: flags change again (example observed: 0x0000000A → 0x0000000D)\
• 0x0C: runtime/battery refresh

NOTE: Flag meanings are TBD; log bit diffs to map over time.

  ----------------------------------------------------------
  RECOMMENDED TEST & VALIDATION PLAN (REGRESSION-PROOFING)
  ----------------------------------------------------------

When implementing any change, run the following tests **in order**.
Record results in the Validation Log section below.

Test Severity Levels: • MUST (required for merging into baseline)\
• SHOULD (recommended)\
• NICE (optional)

## T0 --- Build/Flash/Boot (MUST)

Pass criteria: • Builds cleanly • Flashes cleanly • Boots with expected
banner (version/milestones)

Record: • commit/tag • ESP-IDF version • board/power notes

## T1 --- USB Attach/Enumerate (MUST)

Pass criteria: • NEW_DEV arrives • Device opens • VID:PID matches target
• Config parsing finds HID intf + EP 0x81 MPS 8 • Interface claim
succeeds • INT-IN reader starts

## T2 --- Steady-State Streaming (MUST)

Pass criteria: • INT-IN transfers continue indefinitely • No memory leak
symptoms • No log spam loops • Change-only logs behave as expected

## T3 --- Decode Sanity (MUST)

Pass criteria: • 0x0C decodes batt% and runtime sensibly • 0x13 decodes
AC_present correctly • 0x16 decodes flags (raw u32) correctly • NUT-ish
status block updates only on change

## T4 --- Detach / Reattach (MUST)

Pass criteria: • DEV_GONE handled cleanly • No crash • Re-plug repeats
full attach sequence and resumes streaming

## T5 --- AC Pull / Restore (SHOULD)

Procedure: • Pull AC power (leave load connected) • Wait 20--60 seconds
• Restore AC

Pass criteria: • 0x13 toggles as expected (1→0, then 0→1) • 0x16 changes
(record values) • 0x0C updates (battery/runtime changes may lag)

## T6 --- Long Run (SHOULD)

Procedure: • Run 1--8 hours

Pass criteria: • No stalls • No WDT resets • No USB host errors
accumulating

## T7 --- Feature-Specific Tests (NICE)

Examples: • If adding HTTP: verify endpoint returns JSON and remains
responsive during USB events • If adding NUT TCP: verify basic command
set works while USB streaming continues

  ------------------------------
  VALIDATION LOG (APPEND-ONLY)
  ------------------------------

Use this section to record **test sessions** for historical
traceability. Do not delete entries. Add newest entries at the top.

[2026-03-05] Validation Run — Firmware v9 — (commit/tag: not recorded)
Environment:
• ESP-IDF: v5.3.1-dirty
• Board: ESP32-S3 (16MB flash / 8MB PSRAM)
• UPS: APC VID:PID 051d:0002 (USB HID)
• Notes: Unplug/replug tested; stable enumeration + streaming observed.

Tests:
• T0 Build/Flash/Boot: PASS (banner shows v9)
• T1 Attach/Enumerate: PASS (HID intf 0, EP 0x81 MPS 8, claim OK)
• T2 Streaming: PASS (change-only IN reports repeat)
• T3 Decode Sanity: PASS (0x0C batt/runtime, 0x13 AC, 0x16 flags)
• T4 Detach/Reattach: PASS (DEV_GONE handled; reattach repeats full sequence)
• T5 AC Pull/Restore: NOT RUN
• T6 Long Run: NOT RUN
• T7 Feature-specific: N/A

Observed telemetry (sample):
• 0x06: 06 01 00 08 (len=4)
• 0x0C: 0C 63 38 8B → batt=99% runtime=35640 s
• 0x13: 13 01 → AC present
• 0x14: 14 00 00
• 0x16: 16 0D 00 00 00 → flags=0x0000000D (bits 0,2,3 set)
• 0x21: 21 06

Known issues observed:
• None new

Result:
• Accept as baseline? YES

Template Entry:

\[YYYY-MM-DD\] Validation Run --- Firmware vX --- (commit/tag:
\_\_\_\_\_\_\_\_) Environment: • ESP-IDF: • Board: • UPS cable chain: •
Notes:

Tests: • T0 Build/Flash/Boot: PASS/FAIL (notes) • T1 Attach/Enumerate:
PASS/FAIL (notes) • T2 Streaming: PASS/FAIL (notes) • T3 Decode Sanity:
PASS/FAIL (notes) • T4 Detach/Reattach: PASS/FAIL (notes) • T5 AC
Pull/Restore: PASS/FAIL (notes) • T6 Long Run: PASS/FAIL
(duration/notes) • T7 Feature-specific: PASS/FAIL (notes)

Observed telemetry (optional): • 0x0C: • 0x13: • 0x16: • Other RIDs:
Known issues observed: • KI-\_\_\_ : Result: • Accept as baseline?
YES/NO

  -----------------------
  NEXT MILESTONE TARGET
  -----------------------

Milestone 7 (Firmware v10)

Goals:

• Add decoding scaffolding for remaining report IDs: - 0x06 - 0x14 -
0x21

• Improve status flag interpretation

• Reduce ESP-IDF USB debug log noise

• Maintain stable USB enumeration and streaming behavior

  -------------------------------------------
  WORKFLOW WHEN STARTING A NEW CONVERSATION
  -------------------------------------------

When resuming development with a new assistant session, use the
following workflow.

STEP 1 --- Provide Context Upload or paste: 1) This **Master Engineering
Log** 2) The **current working main.c** 3) Latest serial monitor output
(recommended)

STEP 2 --- Declare Current Baseline State clearly: • main.c CURRENT
VERSION: vX • Current milestone: Milestone X • What is confirmed working
(one paragraph)

STEP 3 --- Define Next Target State the next milestone objective (what
"done" means).

STEP 4 --- Preserve Stable Code Before modifying firmware: • Confirm
current version is stable • Copy the entire verified working main.c into
this log

STEP 5 --- Implement Changes Incrementally • Avoid rewriting working USB
plumbing unless required • Prefer small diffs that are easy to revert

STEP 6 --- Validate Stability (Run the MUST tests) • T0, T1, T2, T3, T4
are required

STEP 7 --- Update Master Log When a milestone is confirmed: 1) Update
the Firmware Evolution Table 2) Update Current Verified Baseline section
3) Update Known Issues table 4) Add a Validation Log entry 5) Paste the
full working main.c

  ---------------------------
  VERIFIED WORKING FIRMWARE
  ---------------------------

main.c CURRENT VERSION: v9

IMPORTANT: Paste the complete **verified working main.c** here whenever
a milestone is confirmed successful.

This guarantees reproducibility and prevents regression loss.

  -----------------------------
  PASTE VERIFIED main.c BELOW
  -----------------------------

  -------------------------------
  
```c
/* main.c CURRENT VERSION: v9
 * Version tracking:
 *   v8 (2026-03-05): Milestone 5 start: maintain decoded UPS state + NUT-style status logs.
 *                   Disable full HID report descriptor dump by default (V7 control submit ESP_ERR_INVALID_ARG).
 *                   Keep usb_host_lib_handle_events() pump + async client callback + change-only IN logging.
 *   v9 (2026-03-05): Milestone 6 start: track remaining common RIDs (0x06/0x14/0x21),
 *                   add RID counters + last-seen timestamps, add flags bit breakdown,
 *                   and add state freshness timestamps (kept out of change-detection to avoid spam).
 *
 * Notes:
 * - This is a full drop-in file (copy/paste replace main.c).
 * - USB plumbing path intentionally kept stable from v8.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"   // v9: esp_timer_get_time()

#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"

// ===================== REVERT INDEX =====================
// REVERT[M3-A-001]: (2026-03-05) Use async client callback + usb_host_client_handle_events(client, timeout) (2 args).
// REVERT[M3-A-002]: (2026-03-05) Do NOT use usb_device_info_t.idVendor/idProduct (fields differ). Use usb_host_get_device_descriptor() (ptr-to-ptr).
// REVERT[M3-A-003]: (2026-03-05) IDF v5.3.1 transfer type is usb_transfer_t (NOT usb_host_transfer_t).
// REVERT[M3-A-004]: (2026-03-05) Endpoint direction test uses (ep & 0x80) instead of missing macros.
// REVERT[M3-A-005]: (2026-03-05) XFERTYPE mask macro is USB_BM_ATTRIBUTES_XFERTYPE_MASK (not *_M).
// REVERT[M3-A-006]: (2026-03-05) NEW_DEV address saved from callback, not hardcoded.
// REVERT[M3-A-007]: (2026-03-05) No usb_host_transfer_cancel / usb_host_release_config_descriptor in v5.3.1.
// REVERT[M3-IN-001]: (2026-03-05) Change-only logging for interrupt IN reports.
// REVERT[M4-D-001]: (2026-03-05) Decode report IDs 0x0C batt/runtime, 0x13 AC, 0x16 flags (little-endian).
// =========================================================

static const char *TAG = "UPS_USB_M3";

#define UPS_VID 0x051d
#define UPS_PID 0x0002

// HID descriptor types
#define USB_DESC_TYPE_HID        0x21
#define USB_DESC_TYPE_HID_REPORT 0x22

// v8+: disable by default because V7 hit ESP_ERR_INVALID_ARG on submit (likely size/path constraint)
#define ENABLE_REPORT_DESCRIPTOR_DUMP 0

static inline uint16_t rd_le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static inline uint32_t rd_le32(const uint8_t *p) { return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// USB host handles
static usb_host_client_handle_t s_client = NULL;
static usb_device_handle_t      s_dev    = NULL;

// Device address from NEW_DEV
static volatile uint8_t s_new_dev_addr = 0;

// HID interface/endpoint
static int      s_hid_intf_num = -1;
static uint8_t  s_ep_in_addr   = 0;
static uint16_t s_ep_in_mps    = 0;
static uint16_t s_hid_report_desc_len = 0;

// Interrupt IN tracking (change-only)
static uint8_t  s_last_in[64];
static int      s_last_in_len = -1;

// v9: report tracking
static uint8_t  s_last_rid = 0;
static uint32_t s_last_report_ms = 0;

static uint32_t s_rid06_count = 0, s_rid14_count = 0, s_rid21_count = 0;
static uint32_t s_rid06_last_ms = 0, s_rid14_last_ms = 0, s_rid21_last_ms = 0;

static uint8_t  s_rid06_last[8]; static int s_rid06_last_len = 0;
static uint8_t  s_rid14_last[8]; static int s_rid14_last_len = 0;
static uint8_t  s_rid21_last[8]; static int s_rid21_last_len = 0;

// Event flags
static volatile bool s_dev_connected = false;
static volatile bool s_dev_gone      = false;

/* =========================================================
 * Milestone 5/6: UPS state model (minimal)
 * ========================================================= */
typedef struct {
    bool     have_batt;
    bool     have_runtime;
    bool     have_ac;
    bool     have_flags;

    uint8_t  battery_charge_pct;   // from 0x0C byte1
    uint32_t battery_runtime_s;     // from 0x0C bytes2..3 (u16) but store in u32
    uint8_t  ac_present;           // from 0x13 byte1 (0/1)
    uint32_t flags;                // from 0x16 bytes1..4 (u32)

    // Derived (best-effort)
    bool     on_battery;           // derived from AC_present (if known)

    // v9: freshness timestamps (ms since boot); excluded from print-equality checks
    uint32_t batt_ms;
    uint32_t runtime_ms;
    uint32_t ac_ms;
    uint32_t flags_ms;
} ups_state_t;

static ups_state_t s_state = {0};
static ups_state_t s_state_last_printed = {0};

static bool state_equals_for_print(const ups_state_t *a, const ups_state_t *b)
{
    // NOTE: timestamps intentionally ignored to avoid status spam when values refresh unchanged.
    return (a->have_batt == b->have_batt) &&
           (a->have_runtime == b->have_runtime) &&
           (a->have_ac == b->have_ac) &&
           (a->have_flags == b->have_flags) &&
           (a->battery_charge_pct == b->battery_charge_pct) &&
           (a->battery_runtime_s == b->battery_runtime_s) &&
           (a->ac_present == b->ac_present) &&
           (a->flags == b->flags) &&
           (a->on_battery == b->on_battery);
}

static void update_derived_state(ups_state_t *st)
{
    if (st->have_ac) {
        st->on_battery = (st->ac_present == 0);
    }
}

static void log_flags_bits(uint32_t flags)
{
    // v9: simple bit view (0..15). Expand later once meanings are known.
    char buf[128];
    int pos = 0;
    for (int b = 0; b < 16 && pos < (int)sizeof(buf) - 8; b++) {
        int set = (int)((flags >> b) & 1U);
        pos += snprintf(&buf[pos], sizeof(buf) - pos, "%d:%d ", b, set);
    }
    ESP_LOGI(TAG, "flags bits[0..15]: %s", buf);
}

static void log_nut_style_if_changed(void)
{
    if (!state_equals_for_print(&s_state, &s_state_last_printed)) {
        s_state_last_printed = s_state;

        const uint32_t tms = now_ms();

        // Best-effort NUT-ish status: OL vs OB
        const char *ups_status = "UNKNOWN";
        if (s_state.have_ac) {
            ups_status = s_state.on_battery ? "OB" : "OL";
        }

        ESP_LOGI(TAG, "----- NUT-ish status (Milestone 5/6) -----");

        if (s_state.have_batt) {
            ESP_LOGI(TAG, "battery.charge: %u", (unsigned)s_state.battery_charge_pct);
            ESP_LOGI(TAG, "battery.charge.age_ms: %" PRIu32, (uint32_t)(tms - s_state.batt_ms));
        } else {
            ESP_LOGI(TAG, "battery.charge: (unknown)");
        }

        if (s_state.have_runtime) {
            ESP_LOGI(TAG, "battery.runtime: %" PRIu32, s_state.battery_runtime_s);
            ESP_LOGI(TAG, "battery.runtime.age_ms: %" PRIu32, (uint32_t)(tms - s_state.runtime_ms));
        } else {
            ESP_LOGI(TAG, "battery.runtime: (unknown)");
        }

        if (s_state.have_ac) {
            ESP_LOGI(TAG, "input.utility.present: %u", (unsigned)s_state.ac_present);
            ESP_LOGI(TAG, "input.utility.present.age_ms: %" PRIu32, (uint32_t)(tms - s_state.ac_ms));
        } else {
            ESP_LOGI(TAG, "input.utility.present: (unknown)");
        }

        if (s_state.have_flags) {
            ESP_LOGI(TAG, "ups.flags: 0x%08" PRIX32, s_state.flags);
            ESP_LOGI(TAG, "ups.flags.age_ms: %" PRIu32, (uint32_t)(tms - s_state.flags_ms));
        } else {
            ESP_LOGI(TAG, "ups.flags: (unknown)");
        }

        ESP_LOGI(TAG, "ups.status: %s", ups_status);

        // v9 driver tracking
        ESP_LOGI(TAG, "driver.last_rid: 0x%02X", (unsigned)s_last_rid);
        ESP_LOGI(TAG, "driver.last_report_ms: %" PRIu32, s_last_report_ms);
        ESP_LOGI(TAG, "driver.rid06.count: %" PRIu32 " last_ms=%" PRIu32, s_rid06_count, s_rid06_last_ms);
        ESP_LOGI(TAG, "driver.rid14.count: %" PRIu32 " last_ms=%" PRIu32, s_rid14_count, s_rid14_last_ms);
        ESP_LOGI(TAG, "driver.rid21.count: %" PRIu32 " last_ms=%" PRIu32, s_rid21_count, s_rid21_last_ms);

        ESP_LOGI(TAG, "----------------------------------------");
    }
}

// Control transfer sync
typedef struct {
    SemaphoreHandle_t done;
    esp_err_t status;
    int actual_num_bytes;
} ctrl_wait_t;

// REVERT[M3-A-003]: transfer type is usb_transfer_t in IDF v5.3.1.
static void ctrl_xfer_cb(usb_transfer_t *t)
{
    ctrl_wait_t *w = (ctrl_wait_t *)t->context;
    if (w) {
        w->status = t->status;
        w->actual_num_bytes = (int)t->actual_num_bytes;
        xSemaphoreGive(w->done);
    }
}

static void intr_in_cb(usb_transfer_t *t);

// ---------- USB Host client event callback ----------
static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    (void)arg;
    if (!event_msg) return;

    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        s_new_dev_addr = event_msg->new_dev.address; // REVERT[M3-A-006]
        s_dev_connected = true;
        ESP_LOGI(TAG, "NEW_DEV received: addr=%u", (unsigned)s_new_dev_addr);
        break;

    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        s_dev_gone = true;
        ESP_LOGW(TAG, "DEV_GONE received");
        break;

    default:
        break;
    }
}

// ---------- Descriptor parsing ----------
static bool parse_hid_intf_and_ep(const uint8_t *cfg, int cfg_len,
                                 int *out_intf, uint8_t *out_ep_in, uint16_t *out_mps,
                                 uint16_t *out_hid_rpt_len)
{
    int cur_intf = -1;
    bool in_hid_intf = false;
    uint16_t hid_rpt_len = 0;
    uint8_t ep_in = 0;
    uint16_t ep_mps = 0;

    int i = 0;
    while (i + 2 <= cfg_len) {
        uint8_t bLength = cfg[i];
        uint8_t bDescriptorType = cfg[i + 1];
        if (bLength < 2) break;
        if (i + bLength > cfg_len) break;

        if (bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE && bLength >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)&cfg[i];
            cur_intf = intf->bInterfaceNumber;

            in_hid_intf = (intf->bInterfaceClass == USB_CLASS_HID);
            if (in_hid_intf) {
                ESP_LOGI(TAG, "HID interface intf=%u alt=%u subclass=0x%02x proto=0x%02x",
                         (unsigned)intf->bInterfaceNumber, (unsigned)intf->bAlternateSetting,
                         (unsigned)intf->bInterfaceSubClass, (unsigned)intf->bInterfaceProtocol);
            }
        } else if (bDescriptorType == USB_DESC_TYPE_HID && in_hid_intf && bLength >= 9) {
            // HID descriptor: [6]=descType [7..8]=descLen (first class descriptor)
            uint8_t descType = cfg[i + 6];
            uint16_t descLen = rd_le16(&cfg[i + 7]);
            if (descType == USB_DESC_TYPE_HID_REPORT) {
                hid_rpt_len = descLen;
                ESP_LOGI(TAG, "HID Report Descriptor length = %u bytes (from HID descriptor 0x21)", (unsigned)hid_rpt_len);
            }
        } else if (bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && in_hid_intf && bLength >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)&cfg[i];

            // REVERT[M3-A-005]: use USB_BM_ATTRIBUTES_XFERTYPE_MASK
            const uint8_t xfer_type = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK);

            // REVERT[M3-A-004]: IN direction test uses bit7 (0x80)
            const bool is_in = ((ep->bEndpointAddress & 0x80) != 0);

            if ((xfer_type == USB_BM_ATTRIBUTES_XFER_INT) && is_in) {
                ep_in = ep->bEndpointAddress;
                ep_mps = ep->wMaxPacketSize;
                ESP_LOGI(TAG, "Interrupt IN EP=0x%02x MPS=%u", (unsigned)ep_in, (unsigned)ep_mps);
            }
        }

        i += bLength;
    }

    if (cur_intf >= 0 && ep_in != 0 && ep_mps != 0 && hid_rpt_len != 0) {
        *out_intf = cur_intf;
        *out_ep_in = ep_in;
        *out_mps = ep_mps;
        *out_hid_rpt_len = hid_rpt_len;
        return true;
    }
    return false;
}

// ---------- Control GET_DESCRIPTOR (Report) ----------
static esp_err_t usb_control_get_descriptor(usb_device_handle_t dev,
                                           uint8_t bmRequestType, uint8_t bRequest,
                                           uint16_t wValue, uint16_t wIndex,
                                           uint8_t *out, int out_len,
                                           int *out_actual)
{
    if (!dev || !out || out_len <= 0) return ESP_ERR_INVALID_ARG;

    usb_transfer_t *t = NULL; // REVERT[M3-A-003]
    const int xfer_len = (int)sizeof(usb_setup_packet_t) + out_len;

    esp_err_t err = usb_host_transfer_alloc(xfer_len, 0, &t);
    if (err != ESP_OK || !t) return err;

    ctrl_wait_t w = {
        .done = xSemaphoreCreateBinary(),
        .status = ESP_FAIL,
        .actual_num_bytes = 0,
    };
    if (!w.done) {
        usb_host_transfer_free(t);
        return ESP_ERR_NO_MEM;
    }

    usb_setup_packet_t *setup = (usb_setup_packet_t *)t->data_buffer;
    setup->bmRequestType = bmRequestType;
    setup->bRequest      = bRequest;
    setup->wValue        = wValue;
    setup->wIndex        = wIndex;
    setup->wLength       = (uint16_t)out_len;

    t->device_handle = dev;
    t->bEndpointAddress = 0; // EP0
    t->callback = ctrl_xfer_cb;
    t->context = &w;
    t->num_bytes = xfer_len;

    err = usb_host_transfer_submit(t);
    if (err != ESP_OK) {
        vSemaphoreDelete(w.done);
        usb_host_transfer_free(t);
        return err;
    }

    xSemaphoreTake(w.done, portMAX_DELAY);

    err = w.status;
    if (err == ESP_OK) {
        int got = w.actual_num_bytes - (int)sizeof(usb_setup_packet_t);
        if (got < 0) got = 0;
        if (got > out_len) got = out_len;
        memcpy(out, (uint8_t *)t->data_buffer + sizeof(usb_setup_packet_t), got);
        if (out_actual) *out_actual = got;
    }

    vSemaphoreDelete(w.done);
    usb_host_transfer_free(t);
    return err;
}

static void hid_get_report_descriptor_and_dump_once(void)
{
#if ENABLE_REPORT_DESCRIPTOR_DUMP
    if (!s_dev || s_hid_intf_num < 0 || s_hid_report_desc_len == 0) return;

    ESP_LOGI(TAG, "Descriptor dump enabled: attempting GET_DESCRIPTOR(report).");
    ESP_LOGI(TAG, "Requesting HID Report Descriptor via control transfer: intf=%d len=%u",
             s_hid_intf_num, (unsigned)s_hid_report_desc_len);

    uint8_t *buf = (uint8_t *)heap_caps_malloc(s_hid_report_desc_len, MALLOC_CAP_DEFAULT);
    if (!buf) {
        ESP_LOGE(TAG, "malloc failed for report descriptor (%u)", (unsigned)s_hid_report_desc_len);
        return;
    }

    int actual = 0;

    esp_err_t err = usb_control_get_descriptor(
        s_dev,
        0x81, 0x06,
        (uint16_t)((USB_DESC_TYPE_HID_REPORT << 8) | 0x00),
        (uint16_t)s_hid_intf_num,
        buf, (int)s_hid_report_desc_len,
        &actual
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET_DESCRIPTOR(report) failed: %s", esp_err_to_name(err));
        heap_caps_free(buf);
        return;
    }

    ESP_LOGI(TAG, "HID Report Descriptor received: %d bytes", actual);
    ESP_LOGI(TAG, "\n===== HID REPORT DESCRIPTOR BEGIN =====");
    for (int i = 0; i < actual; i += 16) {
        char line[16 * 3 + 1];
        int pos = 0;
        int n = (actual - i) > 16 ? 16 : (actual - i);
        for (int j = 0; j < n; j++) {
            pos += snprintf(&line[pos], sizeof(line) - pos, "%02X%s", buf[i + j], (j == n - 1) ? "" : " ");
        }
        ESP_LOGI(TAG, "%s", line);
    }
    ESP_LOGI(TAG, "===== HID REPORT DESCRIPTOR END =====\n");
    heap_caps_free(buf);
#else
    // default: skip to avoid control submit errors; IN reports are sufficient for Milestone 5/6.
    (void)0;
#endif
}

// ---------- Interrupt IN Reader ----------
static void decode_and_update_state(const uint8_t *d, int len)
{
    if (!d || len <= 0) return;

    uint8_t rid = d[0];
    const uint32_t tms = now_ms();

    if (rid == 0x0C && len >= 4) {
        uint8_t batt = d[1];
        uint16_t runtime_s = rd_le16(&d[2]);

        s_state.battery_charge_pct = batt;
        s_state.battery_runtime_s = (uint32_t)runtime_s;
        s_state.have_batt = true;
        s_state.have_runtime = true;
        s_state.batt_ms = tms;
        s_state.runtime_ms = tms;

        ESP_LOGI(TAG, "DECODE 0x0C: batt=%u%% runtime=%u s", (unsigned)batt, (unsigned)runtime_s);

    } else if (rid == 0x13 && len >= 2) {
        uint8_t ac = d[1];
        s_state.ac_present = ac;
        s_state.have_ac = true;
        s_state.ac_ms = tms;

        ESP_LOGI(TAG, "DECODE 0x13: AC_present=%u", (unsigned)ac);

    } else if (rid == 0x16 && len >= 5) {
        uint32_t flags = rd_le32(&d[1]);
        s_state.flags = flags;
        s_state.have_flags = true;
        s_state.flags_ms = tms;

        ESP_LOGI(TAG, "DECODE 0x16: flags=0x%08" PRIX32, flags);
        log_flags_bits(flags);

    } else if (rid == 0x06) {
        s_rid06_count++;
        s_rid06_last_ms = tms;
        s_rid06_last_len = (len > 8) ? 8 : len;
        memcpy(s_rid06_last, d, (size_t)s_rid06_last_len);

        if (len >= 4) {
            ESP_LOGI(TAG, "RID 0x06: b1=%u b2=%u b3=%u (len=%d)",
                     (unsigned)d[1], (unsigned)d[2], (unsigned)d[3], len);
        } else {
            ESP_LOGI(TAG, "RID 0x06: len=%d", len);
        }

    } else if (rid == 0x14) {
        s_rid14_count++;
        s_rid14_last_ms = tms;
        s_rid14_last_len = (len > 8) ? 8 : len;
        memcpy(s_rid14_last, d, (size_t)s_rid14_last_len);

        ESP_LOGI(TAG, "RID 0x14: tracked (unknown payload) len=%d", len);

    } else if (rid == 0x21) {
        s_rid21_count++;
        s_rid21_last_ms = tms;
        s_rid21_last_len = (len > 8) ? 8 : len;
        memcpy(s_rid21_last, d, (size_t)s_rid21_last_len);

        ESP_LOGI(TAG, "RID 0x21: tracked (unknown payload) len=%d", len);
    }

    update_derived_state(&s_state);
    log_nut_style_if_changed();
}

static void intr_in_cb(usb_transfer_t *t)
{
    if (!t) return;

    if (t->status == ESP_OK && t->actual_num_bytes > 0) {
        const uint8_t *d = (const uint8_t *)t->data_buffer;
        int len = (int)t->actual_num_bytes;

        bool changed = (len != s_last_in_len) ||
                       (memcmp(d, s_last_in, (size_t)((len > (int)sizeof(s_last_in)) ? sizeof(s_last_in) : len)) != 0);

        if (changed) {
            // REVERT[M3-IN-001]: change-only logging
            s_last_in_len = len;
            int copy_len = len;
            if (copy_len > (int)sizeof(s_last_in)) copy_len = (int)sizeof(s_last_in);
            memcpy(s_last_in, d, (size_t)copy_len);

            // v9: update last report tracking
            s_last_rid = d[0];
            s_last_report_ms = now_ms();

            char line[64 * 3 + 1];
            int pos = 0;
            for (int i = 0; i < len && i < 64 && pos < (int)sizeof(line) - 4; i++) {
                pos += snprintf(&line[pos], sizeof(line) - pos, "%02X%s", d[i], (i == len - 1) ? "" : " ");
            }
            ESP_LOGI(TAG, "HID IN changed (%d): %s", len, line);

            decode_and_update_state(d, len);
        }
    }

    if (!s_dev_gone) {
        (void)usb_host_transfer_submit(t);
    } else {
        usb_host_transfer_free(t);
    }
}

static esp_err_t start_interrupt_in_reader(void)
{
    if (!s_dev || s_ep_in_addr == 0 || s_ep_in_mps == 0) return ESP_ERR_INVALID_STATE;

    usb_transfer_t *t = NULL;
    esp_err_t err = usb_host_transfer_alloc(s_ep_in_mps, 0, &t);
    if (err != ESP_OK || !t) return err;

    t->device_handle = s_dev;
    t->bEndpointAddress = s_ep_in_addr;
    t->callback = intr_in_cb;
    t->context = NULL;
    t->num_bytes = s_ep_in_mps;

    ESP_LOGI(TAG, "Starting interrupt IN reader: EP=0x%02x MPS=%u (change-only)",
             (unsigned)s_ep_in_addr, (unsigned)s_ep_in_mps);

    return usb_host_transfer_submit(t);
}

/* =========================================================
 * JOB TASK: USB host library event pump
 * Why: Needed for enumeration/host actions so NEW_DEV events fire.
 * ========================================================= */
static void usb_lib_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "usb_host_lib_handle_events: %s", esp_err_to_name(err));
        }
    }
}

/* =========================================================
 * JOB TASK: Main USB client task (open device, claim interface, start reader)
 * ========================================================= */
static void app_main_usb(void *arg)
{
    (void)arg;

    usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_cfg));
    ESP_LOGI(TAG, "usb_host_install OK");

    // v9 optional: reduce internal USB debug noise (keep our TAG logs readable)
    esp_log_level_set("USBH", ESP_LOG_WARN);
    esp_log_level_set("HUB",  ESP_LOG_WARN);

    // Start the library pump (critical for enumeration)
    xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 20, NULL, 0);

    usb_host_client_config_t client_cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };

    ESP_ERROR_CHECK(usb_host_client_register(&client_cfg, &s_client));
    ESP_LOGI(TAG, "usb_host_client_register OK");

    ESP_LOGI(TAG, "Ready. Plug UPS into OTG port now (VBUS must be powered).");
    ESP_LOGI(TAG, "No polling spam: logs will appear only on USB events / changed HID reports.");

    while (1) {
        (void)usb_host_client_handle_events(s_client, portMAX_DELAY);

        if (s_dev_connected) {
            s_dev_connected = false;

            if (s_new_dev_addr == 0) {
                ESP_LOGW(TAG, "NEW_DEV flag set but addr=0? skipping");
                continue;
            }

            usb_device_handle_t dev = NULL;
            esp_err_t err = usb_host_device_open(s_client, s_new_dev_addr, &dev);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "usb_host_device_open(addr=%u) failed: %s",
                         (unsigned)s_new_dev_addr, esp_err_to_name(err));
                continue;
            }
            s_dev = dev;

            // REVERT[M3-A-002]: get_device_descriptor returns const ptr via ptr-to-ptr
            const usb_device_desc_t *dev_desc = NULL;
            err = usb_host_get_device_descriptor(s_dev, &dev_desc);
            if (err != ESP_OK || !dev_desc) {
                ESP_LOGE(TAG, "get_device_descriptor failed: %s", esp_err_to_name(err));
                usb_host_device_close(s_client, s_dev);
                s_dev = NULL;
                continue;
            }

            ESP_LOGI(TAG, "Connected VID:PID=%04x:%04x bDeviceClass=0x%02x EP0_MPS=%u",
                     (unsigned)dev_desc->idVendor, (unsigned)dev_desc->idProduct,
                     (unsigned)dev_desc->bDeviceClass, (unsigned)dev_desc->bMaxPacketSize0);

            if (dev_desc->idVendor != UPS_VID || dev_desc->idProduct != UPS_PID) {
                ESP_LOGW(TAG, "Not target UPS, closing.");
                usb_host_device_close(s_client, s_dev);
                s_dev = NULL;
                continue;
            }

            const usb_config_desc_t *cfg = NULL;
            err = usb_host_get_active_config_descriptor(s_dev, &cfg);
            if (err != ESP_OK || !cfg) {
                ESP_LOGE(TAG, "get_active_config_descriptor failed: %s", esp_err_to_name(err));
                usb_host_device_close(s_client, s_dev);
                s_dev = NULL;
                continue;
            }

            ESP_LOGI(TAG, "Active config wTotalLength=%u", (unsigned)cfg->wTotalLength);

            s_hid_intf_num = -1;
            s_ep_in_addr = 0;
            s_ep_in_mps = 0;
            s_hid_report_desc_len = 0;

            bool ok = parse_hid_intf_and_ep((const uint8_t *)cfg, cfg->wTotalLength,
                                           &s_hid_intf_num, &s_ep_in_addr, &s_ep_in_mps,
                                           &s_hid_report_desc_len);
            if (!ok) {
                ESP_LOGE(TAG, "Failed to locate HID interface/EP/report-len in config descriptor");
                usb_host_device_close(s_client, s_dev);
                s_dev = NULL;
                continue;
            }

            err = usb_host_interface_claim(s_client, s_dev, s_hid_intf_num, 0);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Interface claim failed: %s", esp_err_to_name(err));
                usb_host_device_close(s_client, s_dev);
                s_dev = NULL;
                continue;
            }
            ESP_LOGI(TAG, "Interface claimed");

            // v8/v9 default: no descriptor dump; can enable via ENABLE_REPORT_DESCRIPTOR_DUMP
            hid_get_report_descriptor_and_dump_once();

            // Reset milestone state + v9 counters on attach
            memset(&s_state, 0, sizeof(s_state));
            memset(&s_state_last_printed, 0, sizeof(s_state_last_printed));

            s_last_rid = 0;
            s_last_report_ms = 0;

            s_rid06_count = s_rid14_count = s_rid21_count = 0;
            s_rid06_last_ms = s_rid14_last_ms = s_rid21_last_ms = 0;
            s_rid06_last_len = s_rid14_last_len = s_rid21_last_len = 0;
            memset(s_rid06_last, 0, sizeof(s_rid06_last));
            memset(s_rid14_last, 0, sizeof(s_rid14_last));
            memset(s_rid21_last, 0, sizeof(s_rid21_last));

            ESP_LOGI(TAG, "Milestone 5/6: state reset on attach.");

            err = start_interrupt_in_reader();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "start_interrupt_in_reader failed: %s", esp_err_to_name(err));
            }
        }

        if (s_dev_gone) {
            s_dev_gone = false;

            if (s_dev) {
                if (s_hid_intf_num >= 0) {
                    (void)usb_host_interface_release(s_client, s_dev, s_hid_intf_num);
                }
                (void)usb_host_device_close(s_client, s_dev);
                s_dev = NULL;
            }

            s_new_dev_addr = 0;
            s_hid_intf_num = -1;
            s_ep_in_addr = 0;
            s_ep_in_mps = 0;
            s_hid_report_desc_len = 0;
            s_last_in_len = -1;
            memset(s_last_in, 0, sizeof(s_last_in));

            s_last_rid = 0;
            s_last_report_ms = 0;

            s_rid06_count = s_rid14_count = s_rid21_count = 0;
            s_rid06_last_ms = s_rid14_last_ms = s_rid21_last_ms = 0;
            s_rid06_last_len = s_rid14_last_len = s_rid21_last_len = 0;
            memset(s_rid06_last, 0, sizeof(s_rid06_last));
            memset(s_rid14_last, 0, sizeof(s_rid14_last));
            memset(s_rid21_last, 0, sizeof(s_rid21_last));

            memset(&s_state, 0, sizeof(s_state));
            memset(&s_state_last_printed, 0, sizeof(s_state_last_printed));
            ESP_LOGI(TAG, "Milestone 5/6: state cleared on detach.");
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=======================================");
    ESP_LOGI(TAG, "Milestone 3: HID report monitor (change-only)");
    ESP_LOGI(TAG, "Milestone 4: decode (0x0C batt/runtime, 0x13 AC, 0x16 flags)");
    ESP_LOGI(TAG, "Milestone 5: state + NUT-ish status logs");
    ESP_LOGI(TAG, "Milestone 6: track 0x06/0x14/0x21 + counters + freshness");
    ESP_LOGI(TAG, "ESP-IDF %s", IDF_VER);
    ESP_LOGI(TAG, "main.c CURRENT VERSION: v9");
    ESP_LOGI(TAG, "=======================================");

    xTaskCreatePinnedToCore(app_main_usb, "app_main_usb", 8192, NULL, 10, NULL, 0);
}

/* =========================================================
 * NEXT STEPS
 *
 * v9 (Milestone 6)
 * - [ ] Observe RID 0x06/0x14/0x21 during AC pull/restore and note any field changes
 * - [ ] Start mapping flags bits to meanings (record in Master Engineering Log)
 *
 * v10 (Milestone 7)
 * - [ ] Add HTTP /status endpoint that returns JSON (read-only)
 *
 * v11 (Milestone 8)
 * - [ ] Implement minimal NUT TCP subset on port 3493 (VER, LIST UPS, LIST VAR, GET VAR, USERNAME/PASSWORD, QUIT)
 *
 * v12 (Milestone 9)
 * - [ ] Add web config UI + NVS persistence
 * ========================================================= */
```

END OF MASTER ENGINEERING LOG
  -------------------------------