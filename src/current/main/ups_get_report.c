/*============================================================================
 MODULE: ups_get_report

 RESPONSIBILITY
 - Poll USB Feature reports (GET_REPORT control transfers) for devices
   that expose values only via Feature reports, not interrupt-IN.
 - APC Back-UPS: battery voltage, input voltage, output voltage
 - Results fed into ups_state via ups_state_apply_update()

 APC Back-UPS Feature Report IDs (empirically determined + NUT source):
   0x01  Battery voltage — uint16 LE, unit = 10mV
         e.g. raw=1200 → 12000mV = 12.0V
   0x02  Input voltage  — uint16 LE, unit = 1V (whole volts)
         e.g. raw=120 → 120V (US), raw=230 → 230V (EU)
   0x03  Output voltage — same format as input

 DESIGN — SINGLE USB CLIENT OWNER PATTERN
 =========================================
 The ESP-IDF USB host client handle is NOT safe to use from multiple tasks.
 Only usb_client_task (in ups_usb_hid.c) owns the client handle and may
 call usb_host_transfer_submit_control() and usb_host_client_handle_events().

 ups_get_report works via a FreeRTOS queue:
   1. A timer task (ups_get_report_timer_task) wakes every N seconds and
      posts report IDs to s_request_queue.
   2. usb_client_task calls ups_get_report_service_queue() on each event
      loop iteration. This drains the queue, issues the control transfer,
      pumps events until completion, and decodes the result.
      Because this runs inside usb_client_task, it is the only caller of
      usb_host_transfer_submit_control — no concurrency issue.

 This is the correct single-owner pattern for ESP-IDF USB host.

 VERSION HISTORY
 R0  v15.8  Initial — APC Back-UPS Feature report polling.
 R1  v15.8  Rewrite — single-owner queue pattern to fix USB concurrency.

============================================================================*/

#include "ups_get_report.h"
#include "ups_state.h"

#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "usb/usb_host.h"

static const char *TAG = "ups_get_report";

/* ---- Request queue --------------------------------------------------- */
/* Timer task posts report IDs here; usb_client_task drains and services */

typedef struct {
    uint8_t rid;
} get_report_req_t;

static QueueHandle_t             s_request_queue = NULL;
static usb_host_client_handle_t  s_client        = NULL;
static usb_device_handle_t       s_dev           = NULL;
static int                       s_intf_num      = -1;
static const ups_device_entry_t *s_entry         = NULL;
static TaskHandle_t              s_timer_task    = NULL;
static volatile bool             s_active        = false;

/* ---- Forward declarations -------------------------------------------- */
static void decode_apc_smartups_feature(uint8_t rid, const uint8_t *data, size_t len);

/* ---- Control transfer state ------------------------------------------ */
/*
 * ctrl_cb owns the transfer lifetime from submit until free.
 * The callback copies payload into s_ctrl_payload, frees the transfer,
 * then signals s_ctrl_done. Caller reads s_ctrl_payload after done.
 *
 * s_inflight tracks whether a transfer is registered with the USB host.
 * If non-NULL when stop() is called, the next DEV_GONE will cancel it
 * and ctrl_cb will free it safely. stop() must not free it directly.
 */
#define CTRL_PAYLOAD_MAX 24u
static volatile usb_transfer_t *s_inflight      = NULL;
static volatile bool            s_ctrl_done     = false;
static volatile esp_err_t       s_ctrl_status   = ESP_FAIL;
static volatile uint8_t         s_ctrl_payload[CTRL_PAYLOAD_MAX];
static volatile size_t          s_ctrl_pay_len  = 0;

static void ctrl_cb(usb_transfer_t *t)
{
    if (!t) return;
    s_ctrl_status  = (t->status == USB_TRANSFER_STATUS_COMPLETED) ? ESP_OK : ESP_FAIL;
    s_ctrl_pay_len = 0;
    if (s_ctrl_status == ESP_OK && t->actual_num_bytes > 8u) {
        size_t plen = (size_t)(t->actual_num_bytes - 8u);
        if (plen > CTRL_PAYLOAD_MAX) plen = CTRL_PAYLOAD_MAX;
        memcpy((void *)s_ctrl_payload, t->data_buffer + 8u, plen);
        s_ctrl_pay_len = plen;
    }
    usb_host_transfer_free(t);
    s_inflight  = NULL;
    s_ctrl_done = true;   /* signal last */
}

/* ---- Issue one GET_REPORT — called from usb_client_task only --------- */
/*
 * USB HID GET_REPORT control transfer:
 *   bmRequestType = 0xA1   D→H, Class, Interface
 *   bRequest      = 0x01   GET_REPORT
 *   wValue        = (3 << 8) | rid   type=Feature(3), report_id=rid
 *   wIndex        = interface number
 *   wLength       = report size
 *
 * MUST be called from the same task that owns s_client (usb_client_task).
 * Pumps usb_host_client_handle_events() in-place while waiting.
 */
static esp_err_t do_get_feature_report(uint8_t rid, uint8_t *buf, size_t buf_sz,
                                        size_t *out_len)
{
    if (!s_dev || !s_client || s_intf_num < 0) return ESP_ERR_INVALID_STATE;

    size_t alloc = 8u + buf_sz;
    usb_transfer_t *t = NULL;
    esp_err_t err = usb_host_transfer_alloc(alloc, 0, &t);
    if (err != ESP_OK || !t) {
        ESP_LOGE(TAG, "transfer_alloc rid=0x%02X: %s", rid, esp_err_to_name(err));
        return err;
    }

    uint8_t *s = t->data_buffer;
    s[0] = 0xA1u;
    s[1] = 0x01u;
    s[2] = rid;
    s[3] = 0x03u;   /* Feature report type */
    s[4] = (uint8_t)(s_intf_num);
    s[5] = 0x00u;
    s[6] = (uint8_t)(buf_sz & 0xFFu);
    s[7] = (uint8_t)(buf_sz >> 8u);

    t->device_handle    = s_dev;
    t->bEndpointAddress = 0x00u;
    t->callback         = ctrl_cb;
    t->context          = NULL;
    t->num_bytes        = alloc;

    s_ctrl_done    = false;
    s_ctrl_status  = ESP_FAIL;
    s_ctrl_pay_len = 0;
    s_inflight     = t;   /* track for stop() to detect pending transfer */

    err = usb_host_transfer_submit_control(s_client, t);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "submit_control rid=0x%02X: %s", rid, esp_err_to_name(err));
        s_inflight = NULL;
        usb_host_transfer_free(t);
        return err;
    }

    /* Pump USB event loop until callback fires.
     * ctrl_cb always frees the transfer — caller must NOT free t after this point.
     * On DEV_GONE the host cancels all transfers and fires ctrl_cb with error status.
     * Max wait: 3000ms (600 x 5ms) - increased from 1500ms to handle slow APC responses. */
    const TickType_t slice = pdMS_TO_TICKS(5);
    const int        max   = 600;
    for (int i = 0; i < max && !s_ctrl_done; i++) {
        usb_host_client_handle_events(s_client, 0);
        vTaskDelay(slice);
    }

    if (!s_ctrl_done) {
        /* Genuine timeout: callback never fired. Transfer is still registered
         * with the USB host. s_inflight remains set; on next DEV_GONE the
         * host will cancel the transfer and ctrl_cb will free it safely. */
        ESP_LOGW(TAG, "GET_REPORT rid=0x%02X: timed out - transfer pending until DEV_GONE", rid);
        return ESP_FAIL;
    }

    /* ctrl_cb has fired and freed the transfer. t is no longer valid.
     * Payload was copied into s_ctrl_payload before the free. */
    if (s_ctrl_status != ESP_OK) {
        ESP_LOGW(TAG, "GET_REPORT rid=0x%02X: transfer failed", rid);
        return ESP_FAIL;
    }

    size_t plen = s_ctrl_pay_len;
    if (plen > buf_sz) plen = buf_sz;
    if (plen == 0) {
        ESP_LOGW(TAG, "GET_REPORT rid=0x%02X: empty payload", rid);
        return ESP_FAIL;
    }
    memcpy(buf, (const void *)s_ctrl_payload, plen);
    if (out_len) *out_len = plen;
    return ESP_OK;
}

/* ---- Decode APC Feature reports -------------------------------------- */
static void decode_apc_feature(uint8_t rid, const uint8_t *data, size_t len)
{
    /* HID GET_REPORT responses include the report ID as byte 0 of the payload.
     * So for a descriptor field declared as N bytes, the response is N+1 bytes:
     *   data[0] = report ID (echo)
     *   data[1..N] = actual field data
     *
     * rid=0x07 descriptor (after rid byte):
     *   bytes 1-2: uint16 LE  uid=0073 page=85 → BatteryVoltage
     *   bytes 3-4: uint16 LE  uid=004B page=85 → ConfigVoltage (nominal)
     *   bytes 5-6: uint16 LE  uid=0065 page=84 → APC vendor (likely input V)
     *   bytes 7-8: uint16 LE  uid=00DB page=85 → ACVoltage (output V)
     * Total response: 9 bytes [rid, b1, b2, b3, b4, b5, b6, b7, b8]
     */

    /* Log full raw bytes regardless of rid */
    {
        char hexbuf[64] = {0};
        int  pos = 0;
        size_t n = (len > 16u) ? 16u : len;
        for (size_t i = 0; i < n; i++) {
            pos += snprintf(hexbuf + pos, sizeof(hexbuf) - (size_t)pos,
                            "%02X%s", data[i], (i == n-1u) ? "" : " ");
        }
        ESP_LOGI(TAG, "[APC Feature] rid=0x%02X len=%u bytes: %s", rid, (unsigned)len, hexbuf);
    }

    switch (rid) {
    case 0x17: {
        /* rid=0x17: AC line voltage
         * descriptor: page=85 uid=002A size=16 (ConfigVoltage/LineVoltage)
         * response: [0x17, lo, hi]  — uint16 LE, unit = 1V (whole volts)
         * confirmed: 0x0078 = 120 → 120V US mains
         * On-line (OL): input.voltage = output.voltage = this value
         * On-battery (OB): input.voltage = 0 or absent; output.voltage = this value
         * Sanity: 80..300V
         */
        if (len < 3u) {
            ESP_LOGW(TAG, "[APC Feature] rid=0x17: only %u bytes (need 3)", (unsigned)len);
            break;
        }
        uint16_t volts = (uint16_t)(data[1] | ((uint16_t)data[2] << 8u));
        ESP_LOGI(TAG, "[APC Feature] rid=0x17 line_voltage=%uV (raw=%u)", volts, volts);

        if (volts >= 80u && volts <= 300u) {
            uint32_t mv = (uint32_t)volts * 1000u;
            ups_state_update_t upd;
            memset(&upd, 0, sizeof(upd));
            upd.valid                = true;
            upd.input_voltage_valid  = true;
            upd.input_voltage_mv     = mv;
            upd.output_voltage_valid = true;
            upd.output_voltage_mv    = mv;
            ups_state_apply_update(&upd);
            ESP_LOGI(TAG, "[APC Feature] input.voltage = output.voltage = %uV", volts);
        } else if (volts == 0u) {
            /* On-battery: clear input voltage; output still driven by inverter.
             * We don't have separate output-only measurement so leave both clear. */
            ups_state_update_t upd;
            memset(&upd, 0, sizeof(upd));
            upd.valid                = true;
            upd.input_voltage_valid  = false;
            upd.output_voltage_valid = false;
            ups_state_apply_update(&upd);
            ESP_LOGI(TAG, "[APC Feature] line_voltage=0 — on battery, clearing input/output voltage");
        } else {
            ESP_LOGW(TAG, "[APC Feature] rid=0x17 volts=%u — outside 80-300V, ignoring", volts);
        }
        break;
    }
    default:
        /* Unexpected rid from probe list — logged above, nothing to decode */
        break;
    }
}

/* ---- Report ID lists ------------------------------------------------- */
/* APC Back-UPS: rid=0x07 is the voltage Feature report (8 bytes, 4×uint16 LE)
 * Fields per HID descriptor (after report-ID byte at offset 0):
 *   bytes 1-2: uid=0073 page=85 → Voltage (battery voltage)
 *   bytes 3-4: uid=004B page=85 → ConfigVoltage
 *   bytes 5-6: uid=0065 page=84 → APC vendor (input or output voltage)
 *   bytes 7-8: uid=00DB page=85 → Voltage (AC output voltage)
 *
 * Note: HID GET_REPORT response prepends the report ID as byte 0,
 * so the 8-byte payload we receive is: [rid, v0_lo, v0_hi, v1_lo, v1_hi,
 *                                        v2_lo, v2_hi, v3_lo, v3_hi]
 *
 * rids 0x01/0x02/0x03 exist in the descriptor but return only the report
 * ID echo + 1 data byte of indeterminate meaning (not voltages). Skip them.
 */
/* Probe a wide range of APC Feature report IDs to find voltage data.
 * Results logged at INFO level for raw analysis — decode will follow.
 * rids in descriptor: 0x01-0x07, 0x17, 0x36, 0x40-0x42, 0x50, 0x52
 * rids seen in interrupt-IN but not in descriptor:
 *   0x20, 0x21, 0x22, 0x23, 0x25, 0x28, 0x29, 0x82, 0x85-0x88
 * We probe a representative set here. */
/* Production: rid=0x17 only (confirmed = AC line voltage, 120V US / 230V EU)
 * Battery voltage is not available via GET_REPORT on APC Back-UPS (0002)
 * firmware — all high rids (0x82-0x88) STALL on this device. */
static const uint8_t s_apc_rids[]          = { 0x17 };
static const size_t  s_apc_rids_n          = sizeof(s_apc_rids) / sizeof(s_apc_rids[0]);
/* APC Smart-UPS (PID 0003) Feature rids:
 *   rid=0x06  charging flag (byte[1]) + discharging flag (byte[2])
 *   rid=0x0E  battery.voltage (byte[1], raw value)
 * Runtime arrives on interrupt-IN (rid=0x0D) so no GET_REPORT needed. */
static const uint8_t s_apc_smartups_rids[] = { 0x06, 0x0E };
static const size_t  s_apc_smartups_rids_n = sizeof(s_apc_smartups_rids) / sizeof(s_apc_smartups_rids[0]);
/* Eaton/MGE (PID FFFF) Feature rids:
 *   rid=0x20  battery.charge - byte[1] = charge% (0-100)
 *             Confirmed from two 3S 700 submissions (2026-03-30).
 *             Response: [0x20, charge_pct]  e.g. [0x20, 0x02] = 2%
 *   rid=0xFD  unknown - returns 2 bytes [0xFD, 0x29], short read
 *             Not decoded yet - logged for future analysis.
 */
static const uint8_t s_eaton_rids[]        = { 0x20, 0xFD };
static const size_t  s_eaton_rids_n        = sizeof(s_eaton_rids) / sizeof(s_eaton_rids[0]);
static const uint8_t s_tripplite_rids[]    = { 0x01, 0x0C };
static const size_t  s_tripplite_rids_n    = sizeof(s_tripplite_rids) / sizeof(s_tripplite_rids[0]);

/* ---- Decode APC Smart-UPS Feature reports ----------------------------- */
static void decode_apc_smartups_feature(uint8_t rid, const uint8_t *data, size_t len)
{
    char hexbuf[48] = {0};
    int  pos = 0;
    size_t n = (len > 8u) ? 8u : len;
    for (size_t i = 0; i < n; i++) {
        pos += snprintf(hexbuf + pos, sizeof(hexbuf) - (size_t)pos,
                        "%02X%s", data[i], (i == n-1u) ? "" : " ");
    }
    ESP_LOGI(TAG, "[SMRT Feature] rid=0x%02X len=%u: %s", rid, (unsigned)len, hexbuf);

    switch (rid) {
    case 0x06: {
        if (len < 3u) {
            ESP_LOGW(TAG, "[SMRT Feature] rid=0x06: short read %u bytes", (unsigned)len);
            break;
        }
        bool charging    = (data[1] != 0u);
        bool discharging = (data[2] != 0u);
        ESP_LOGI(TAG, "[SMRT Feature] rid=0x06 charging=%u discharging=%u",
                 (unsigned)charging, (unsigned)discharging);
        ups_state_update_t upd;
        memset(&upd, 0, sizeof(upd));
        upd.valid           = true;
        upd.ups_flags_valid = true;
        if (charging)    upd.ups_flags |= 0x01u;
        if (discharging) upd.ups_flags |= 0x02u;
        upd.input_utility_present_valid = true;
        upd.input_utility_present       = !discharging;
        ups_state_apply_update(&upd);
        break;
    }
    case 0x0E: {
        if (len < 2u) {
            ESP_LOGW(TAG, "[SMRT Feature] rid=0x0E: short read %u bytes", (unsigned)len);
            break;
        }
        uint8_t raw = data[1];
        /* raw = whole volts (tentative). Sanity: 8..60V for 12/24/48V systems. */
        if (raw >= 8u && raw <= 60u) {
            ups_state_update_t upd;
            memset(&upd, 0, sizeof(upd));
            upd.valid                 = true;
            upd.battery_voltage_valid = true;
            upd.battery_voltage_mv    = (uint32_t)raw * 1000u;
            ups_state_apply_update(&upd);
            ESP_LOGI(TAG, "[SMRT Feature] battery.voltage=%uV", (unsigned)raw);
        } else {
            ESP_LOGW(TAG, "[SMRT Feature] rid=0x0E raw=%u outside 8-60V - ignoring", (unsigned)raw);
        }
        break;
    }
    default:
        break;
    }
}

/* ---- Decode Eaton/MGE Feature reports -------------------------------- */
static void decode_eaton_feature(uint8_t rid, const uint8_t *data, size_t len)
{
    char hexbuf[32] = {0};
    int  pos = 0;
    size_t n = (len > 8u) ? 8u : len;
    for (size_t i = 0; i < n; i++) {
        pos += snprintf(hexbuf + pos, sizeof(hexbuf) - (size_t)pos,
                        "%02X%s", data[i], (i == n-1u) ? "" : " ");
    }
    ESP_LOGI(TAG, "[MGE Feature] rid=0x%02X len=%u: %s", rid, (unsigned)len, hexbuf);

    switch (rid) {
    case 0x20: {
        /*
         * rid=0x20 = battery.charge (confirmed Eaton 3S 700, 2026-03-30)
         * Response: [0x20, charge_pct]  byte[1] = charge 0-100%
         * Two submissions both returned 0x02 (2%) - battery was depleted.
         */
        if (len < 2u) {
            ESP_LOGW(TAG, "[MGE Feature] rid=0x20: short read %u bytes", (unsigned)len);
            break;
        }
        uint8_t charge = data[1];
        if (charge <= 100u) {
            ups_state_update_t upd;
            memset(&upd, 0, sizeof(upd));
            upd.valid                 = true;
            upd.battery_charge_valid  = true;
            upd.battery_charge        = charge;
            ups_state_apply_update(&upd);
            ESP_LOGI(TAG, "[MGE Feature] battery.charge=%u%%", (unsigned)charge);
        } else {
            ESP_LOGW(TAG, "[MGE Feature] rid=0x20 charge=%u - out of range, ignoring", (unsigned)charge);
        }
        break;
    }
    case 0xFD:
        /*
         * rid=0xFD: returns 2 bytes [0xFD, 0x29] on Eaton 3S 700.
         * Meaning unknown. Logged above for analysis. No decode yet.
         * Candidates: firmware version, battery status flags, runtime low threshold.
         */
        if (len < 2u) {
            ESP_LOGW(TAG, "[MGE Feature] rid=0xFD: short read %u bytes", (unsigned)len);
        }
        /* No decode - raw log above is sufficient for now */
        break;
    default:
        break;
    }
}

/* ---- Timer task - only posts to queue, does NO USB work -------------- */
static void get_report_timer_task(void *arg)
{
    (void)arg;

    const uint8_t *rids   = NULL;
    size_t         rids_n = 0;

    if (s_entry && s_entry->decode_mode == DECODE_APC_BACKUPS) {
        rids   = s_apc_rids;
        rids_n = s_apc_rids_n;
    } else if (s_entry && s_entry->decode_mode == DECODE_APC_SMARTUPS) {
        rids   = s_apc_smartups_rids;
        rids_n = s_apc_smartups_rids_n;
    } else if (s_entry && s_entry->decode_mode == DECODE_EATON_MGE) {
        rids   = s_eaton_rids;
        rids_n = s_eaton_rids_n;
    } else {
        rids   = s_tripplite_rids;
        rids_n = s_tripplite_rids_n;
    }

    ESP_LOGI(TAG, "Timer task started (%u RIDs, interval=%dms)",
             (unsigned)rids_n, UPS_GET_REPORT_INTERVAL_MS);

    /* Initial delay — let interrupt-IN reader stabilise */
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (s_active) {
        /* Post each RID to the queue — usb_client_task will service them */
        for (size_t i = 0; i < rids_n && s_active; i++) {
            get_report_req_t req = { .rid = rids[i] };
            if (xQueueSend(s_request_queue, &req, pdMS_TO_TICKS(500)) != pdTRUE) {
                ESP_LOGW(TAG, "request queue full, dropping rid=0x%02X", rids[i]);
            }
        }
        /* Sleep until next cycle */
        for (int ms = 0; ms < UPS_GET_REPORT_INTERVAL_MS && s_active; ms += 100) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "Timer task exiting");
    s_timer_task = NULL;
    vTaskDelete(NULL);
}

/* ---- Public API ------------------------------------------------------- */

/*
 * ups_get_report_service_queue — called by usb_client_task on every iteration.
 *
 * Drains up to one pending request per call (avoids blocking the USB event
 * loop for too long — the control transfer pump is ~5ms per slice × 300 max
 * = 1.5s worst case, which is acceptable since usb_client_task uses
 * portMAX_DELAY on usb_host_client_handle_events only when idle).
 */
void ups_get_report_service_queue(void)
{
    if (!s_active || !s_request_queue || !s_dev) return;

    get_report_req_t req;
    /* Non-blocking peek — if nothing pending, return immediately */
    if (xQueueReceive(s_request_queue, &req, 0) != pdTRUE) return;

    /* Request size must match what the device expects in wLength.
     * Requesting too many bytes causes some devices to STALL the control
     * transfer rather than returning a short response.
     *
     * Eaton/MGE: rid=0x20 is declared as 1 byte in the descriptor (plus
     * 1 rid echo byte = 2 bytes total). Requesting 16 causes a STALL/timeout.
     * Use 2 bytes for Eaton to exactly match the declared report size.
     *
     * APC / others: 16 bytes is safe — their Feature reports are larger
     * and the devices tolerate over-sized wLength requests. */
    uint8_t buf[16];
    size_t  buf_sz = sizeof(buf);
    if (s_entry && s_entry->decode_mode == DECODE_EATON_MGE) {
        buf_sz = 2u;  /* rid=0x20: 1 rid echo + 1 charge byte; rid=0xFD: same */
    }
    size_t  got = 0;
    esp_err_t err = do_get_feature_report(req.rid, buf, buf_sz, &got);
    if (err == ESP_OK && got > 0) {
        if (s_entry && s_entry->decode_mode == DECODE_APC_BACKUPS) {
            decode_apc_feature(req.rid, buf, got);
        } else if (s_entry && s_entry->decode_mode == DECODE_APC_SMARTUPS) {
            decode_apc_smartups_feature(req.rid, buf, got);
        } else if (s_entry && s_entry->decode_mode == DECODE_EATON_MGE) {
            decode_eaton_feature(req.rid, buf, got);
        }
        /* Future: add Tripp Lite decode branch here */
    }
}

void ups_get_report_start(usb_host_client_handle_t client,
                          usb_device_handle_t      dev,
                          int                      intf_num,
                          const ups_device_entry_t *entry)
{
    if (s_active) {
        ESP_LOGW(TAG, "already active — stop first");
        return;
    }
    if (!client || !dev || intf_num < 0 || !entry) {
        ESP_LOGW(TAG, "start: invalid args");
        return;
    }

    s_client   = client;
    s_dev      = dev;
    s_intf_num = intf_num;
    s_entry    = entry;
    s_active   = true;

    if (!s_request_queue) {
        s_request_queue = xQueueCreate(8, sizeof(get_report_req_t));
        if (!s_request_queue) {
            ESP_LOGE(TAG, "failed to create request queue");
            s_active = false;
            return;
        }
    } else {
        xQueueReset(s_request_queue);
    }

    ESP_LOGI(TAG, "Starting for %s (quirks=0x%04X, mode=%d)",
             entry->vendor_name ? entry->vendor_name : "?",
             (unsigned)entry->quirks,
             (int)entry->decode_mode);

    BaseType_t ret = xTaskCreatePinnedToCore(get_report_timer_task, "gr_timer",
                                              2048, NULL, 3, &s_timer_task, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create timer task");
        s_active      = false;
        s_timer_task  = NULL;
    }
}

void ups_get_report_stop(void)
{
    if (!s_active) return;
    ESP_LOGI(TAG, "Stopping...");
    s_active   = false;
    s_dev      = NULL;
    s_client   = NULL;
    s_intf_num = -1;
    s_entry    = NULL;
    /* Timer task will see s_active=false and self-delete.
     * If a control transfer is in-flight (s_inflight != NULL), ctrl_cb will
     * free it when the USB host cancels it during its own DEV_GONE teardown.
     * Do NOT wait here - this function is called from cleanup_device() which
     * runs in usb_client_task. Waiting for ctrl_cb here would deadlock because
     * ctrl_cb fires via usb_host_client_handle_events() which only runs in
     * the same usb_client_task that is blocked waiting here. */
    if (s_inflight != NULL) {
        ESP_LOGI(TAG, "Stop: transfer in-flight, will be freed by ctrl_cb on USB teardown");
    }
}

bool ups_get_report_running(void)
{
    return s_active;
}
