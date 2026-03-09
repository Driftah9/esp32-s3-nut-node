/*============================================================================
 MODULE: ups_usb_hid

 RESPONSIBILITY
 - USB Host lifecycle
 - Detect NEW_DEV/DEV_GONE
 - Log VID:PID, parse HID interface descriptor, IN endpoint
 - Fetch HID Report Descriptor via USB control transfer
 - Parse report descriptor → ups_hid_parser_set_descriptor()
 - Start interrupt-IN change-only reader
 - Recover cleanly on unplug so reattach can be detected
 - Feed decoded HID packets into ups_state via ups_hid_parser
 - Read cached USB string descriptors for mfr/model/serial

 REVERT HISTORY
 R0  v14.7  USB skeleton placeholder
 R1  v14.8  scan + identify + claim + INT-IN logging
 R2  v14.8.1 remove false-disabled compile-time stub
 R3  v14.8.2 reattach recovery cleanup path
 R4  v14.9  parser + ups_state integration
 R5  v14.10 candidate metric path retained
 R6  v14.11 cached USB string descriptor reader drop-in for v14.2
 R7  v14.15 restored complete module from v14.3 stable
 R8  v14.16 call ups_state_on_usb_disconnect() in cleanup_device()
 R9  v15.0  Add fetch_and_parse_report_descriptor() — fetches the HID
            Report Descriptor via USB GET_DESCRIPTOR (type 0x22) control
            transfer, feeds it to ups_hid_desc_parse(), then calls
            ups_hid_parser_set_descriptor().  This replaces all hardcoded
            APC report-ID logic and makes the driver vendor-agnostic.
 R10 v15.1  Fix control transfer timeout: pump usb_host_client_handle_events()
            while waiting for the transfer callback instead of blocking with
            xTaskNotifyWait().  The USB host event loop must be serviced
            for the control transfer completion callback to fire.

============================================================================*/

#include "ups_usb_hid.h"

#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"
#include "usb/usb_types_stack.h"
#include "usb/usb_helpers.h"

#include "ups_hid_parser.h"
#include "ups_hid_desc.h"
#include "ups_state.h"
#include "ups_device_db.h"
#include "ups_get_report.h"

static const char *TAG = "ups_usb_hid";

/* USB descriptor types */
#define USB_DESC_TYPE_HID         0x21
#define USB_DESC_TYPE_HID_REPORT  0x22

/* Maximum report descriptor size we'll attempt to fetch.
 * Typical UPS: 200–900 bytes.  APC XS1500M: ~350 bytes.
 * CyberPower SX550G: ~450 bytes (estimated).
 * We allocate on heap, so 2048 is a safe ceiling. */
#define HID_REPORT_DESC_MAX_BYTES  2048

#ifndef UPS_USB_ENABLE_INTR_IN_READER
#define UPS_USB_ENABLE_INTR_IN_READER 1
#endif

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} usb_hid_desc_t;

static usb_host_client_handle_t s_client = NULL;
static usb_device_handle_t      s_dev    = NULL;

static volatile bool    s_dev_connected = false;
static volatile bool    s_dev_gone      = false;
static volatile uint8_t s_new_dev_addr  = 0;

static int      s_hid_intf_num = -1;
static int      s_hid_alt      = 0;
static uint8_t  s_ep_in_addr   = 0;
static uint16_t s_ep_in_mps    = 0;
static uint16_t s_hid_rpt_len  = 0;
static uint16_t s_vid          = 0;
static uint16_t s_pid          = 0;

static char     s_mfr[64];
static char     s_product[64];
static char     s_serial[64];

static uint8_t  s_last_in[64];
static int      s_last_in_len = -1;

/* -------------------------------------------------------------------------
 Helpers
------------------------------------------------------------------------- */

static void set_identity_defaults(void)
{
    s_mfr[0] = 0;
    s_product[0] = 0;
    s_serial[0] = 0;

    /* Best-guess manufacturer from VID */
    if (s_vid == 0x051d) {
        strlcpy(s_mfr, "APC", sizeof(s_mfr));
    } else if (s_vid == 0x0764) {
        strlcpy(s_mfr, "CyberPower", sizeof(s_mfr));
    } else if (s_vid == 0x0463) {
        strlcpy(s_mfr, "Eaton/MGE", sizeof(s_mfr));
    } else if (s_vid == 0x09ae) {
        strlcpy(s_mfr, "Tripp Lite", sizeof(s_mfr));
    } else if (s_vid == 0x0665) {
        strlcpy(s_mfr, "Powercom", sizeof(s_mfr));
    } else {
        strlcpy(s_mfr, "UNKNOWN", sizeof(s_mfr));
    }
    strlcpy(s_product, "UNKNOWN", sizeof(s_product));
    strlcpy(s_serial, "UNKNOWN", sizeof(s_serial));
}

static void publish_identity(void)
{
    ups_state_set_usb_identity(s_vid, s_pid, s_hid_rpt_len,
                               s_mfr, s_product, s_serial);
}

static void reset_session(void)
{
    s_hid_intf_num = -1;
    s_hid_alt      = 0;
    s_ep_in_addr   = 0;
    s_ep_in_mps    = 0;
    s_hid_rpt_len  = 0;
    s_vid          = 0;
    s_pid          = 0;
    s_last_in_len  = -1;
    memset(s_last_in, 0, sizeof(s_last_in));
    set_identity_defaults();
    ups_hid_parser_reset();
}

static void cleanup_device(void)
{
    if (s_dev != NULL) {
        if (s_hid_intf_num >= 0) {
            esp_err_t rel = usb_host_interface_release(s_client, s_dev,
                                                        (uint8_t)s_hid_intf_num);
            if (rel != ESP_OK) {
                ESP_LOGW(TAG, "usb_host_interface_release: %s", esp_err_to_name(rel));
            }
        }
        esp_err_t cerr = usb_host_device_close(s_client, s_dev);
        if (cerr != ESP_OK) {
            ESP_LOGW(TAG, "usb_host_device_close: %s", esp_err_to_name(cerr));
        }
        s_dev = NULL;
    }

    s_dev_connected = false;
    s_dev_gone      = false;
    s_new_dev_addr  = 0;

    /* Stop GET_REPORT polling before clearing device state */
    ups_get_report_stop();

    reset_session();
    ups_state_on_usb_disconnect();

    ESP_LOGI(TAG, "USB device cleanup complete. Waiting for NEW_DEV...");
}

/* -------------------------------------------------------------------------
 USB event callback
------------------------------------------------------------------------- */
static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    (void)arg;
    if (!event_msg) return;
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        s_new_dev_addr  = event_msg->new_dev.address;
        s_dev_connected = true;
        ESP_EARLY_LOGI(TAG, "NEW_DEV addr=%u", (unsigned)s_new_dev_addr);
    } else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        s_dev_gone = true;
        ESP_EARLY_LOGW(TAG, "DEV_GONE");
    }
}

/* -------------------------------------------------------------------------
 USB lib task
------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------
 USB string descriptor → ASCII
------------------------------------------------------------------------- */
static void usb_str_desc_to_ascii(const usb_str_desc_t *src, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    out[0] = 0;
    if (!src) return;
    size_t pos = 0;
    size_t wchar_count = (src->bLength >= 2U) ? ((src->bLength - 2U) / 2U) : 0U;
    for (size_t i = 0; i < wchar_count && pos + 1U < out_sz; i++) {
        uint16_t ch = src->wData[i];
        out[pos++] = (ch >= 32U && ch <= 126U) ? (char)ch : '?';
    }
    out[pos] = 0;
}

/* -------------------------------------------------------------------------
 Load USB string descriptors
------------------------------------------------------------------------- */
static void load_usb_strings(usb_device_handle_t dev)
{
    usb_device_info_t dev_info;
    memset(&dev_info, 0, sizeof(dev_info));
    esp_err_t err = usb_host_device_info(dev, &dev_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb_host_device_info failed: %s", esp_err_to_name(err));
        return;
    }

    if (dev_info.str_desc_manufacturer)
        usb_str_desc_to_ascii(dev_info.str_desc_manufacturer, s_mfr, sizeof(s_mfr));
    if (dev_info.str_desc_product)
        usb_str_desc_to_ascii(dev_info.str_desc_product, s_product, sizeof(s_product));
    if (dev_info.str_desc_serial_num)
        usb_str_desc_to_ascii(dev_info.str_desc_serial_num, s_serial, sizeof(s_serial));

    if (s_mfr[0] == 0)     set_identity_defaults();  /* keep VID-guessed mfr */
    if (s_product[0] == 0) strlcpy(s_product, "UNKNOWN", sizeof(s_product));
    if (s_serial[0]  == 0) strlcpy(s_serial,  "UNKNOWN", sizeof(s_serial));

    ESP_LOGI(TAG, "USB strings: mfr='%s' product='%s' serial='%s'",
             s_mfr, s_product, s_serial);
    publish_identity();
}

/* -------------------------------------------------------------------------
 Log device descriptor
------------------------------------------------------------------------- */
static void log_device_descriptor(usb_device_handle_t dev)
{
    const usb_device_desc_t *dd = NULL;
    esp_err_t err = usb_host_get_device_descriptor(dev, &dd);
    if (err != ESP_OK || dd == NULL) {
        ESP_LOGE(TAG, "usb_host_get_device_descriptor failed: %s", esp_err_to_name(err));
        return;
    }
    s_vid = dd->idVendor;
    s_pid = dd->idProduct;
    set_identity_defaults();

    ESP_LOGI(TAG, "USB Device: VID:PID=%04X:%04X bcdUSB=%04X class=%02X sub=%02X proto=%02X",
             (unsigned)dd->idVendor, (unsigned)dd->idProduct, (unsigned)dd->bcdUSB,
             (unsigned)dd->bDeviceClass, (unsigned)dd->bDeviceSubClass,
             (unsigned)dd->bDeviceProtocol);
    publish_identity();
}

/* -------------------------------------------------------------------------
 Parse active configuration to find HID interface + endpoints
------------------------------------------------------------------------- */
static esp_err_t parse_active_config(usb_device_handle_t dev)
{
    const usb_config_desc_t *cfg = NULL;
    esp_err_t err = usb_host_get_active_config_descriptor(dev, &cfg);
    if (err != ESP_OK || cfg == NULL) {
        ESP_LOGE(TAG, "usb_host_get_active_config_descriptor: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Active config wTotalLength=%u bNumInterfaces=%u",
             (unsigned)cfg->wTotalLength, (unsigned)cfg->bNumInterfaces);

    const uint8_t *p     = (const uint8_t *)cfg;
    const uint16_t total = cfg->wTotalLength;
    bool in_hid_intf = false;
    s_hid_intf_num = -1;

    for (uint16_t i = 0; i + 2U <= total;) {
        const uint8_t bLength = p[i];
        const uint8_t bType   = p[i + 1];

        if (bLength == 0U) break;
        if ((uint16_t)(i + bLength) > total) break;

        if (bType == USB_B_DESCRIPTOR_TYPE_INTERFACE && bLength >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)&p[i];
            in_hid_intf = (intf->bInterfaceClass == USB_CLASS_HID);
            if (in_hid_intf) {
                s_hid_intf_num = intf->bInterfaceNumber;
                s_hid_alt      = intf->bAlternateSetting;
                ESP_LOGI(TAG, "HID interface: intf=%u alt=%u sub=%02X proto=%02X",
                         (unsigned)s_hid_intf_num, (unsigned)s_hid_alt,
                         (unsigned)intf->bInterfaceSubClass,
                         (unsigned)intf->bInterfaceProtocol);
            }
        } else if (bType == USB_DESC_TYPE_HID && in_hid_intf &&
                   bLength >= sizeof(usb_hid_desc_t)) {
            const usb_hid_desc_t *hid = (const usb_hid_desc_t *)&p[i];
            s_hid_rpt_len = hid->wReportDescriptorLength;
            ESP_LOGI(TAG, "HID Report Descriptor length = %u bytes", (unsigned)s_hid_rpt_len);
            publish_identity();
        } else if (bType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && in_hid_intf &&
                   bLength >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)&p[i];
            const uint8_t xfer_type = ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK;
            const bool is_in = (ep->bEndpointAddress & 0x80U) != 0U;
            if (is_in && xfer_type == USB_BM_ATTRIBUTES_XFER_INT) {
                s_ep_in_addr = ep->bEndpointAddress;
                s_ep_in_mps  = ep->wMaxPacketSize;
                ESP_LOGI(TAG, "Interrupt IN EP=0x%02X MPS=%u interval=%u",
                         (unsigned)s_ep_in_addr, (unsigned)s_ep_in_mps,
                         (unsigned)ep->bInterval);
            }
        }
        i = (uint16_t)(i + bLength);
    }

    if (s_hid_intf_num < 0) {
        ESP_LOGW(TAG, "No HID interface found");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 Fetch HID Report Descriptor via control transfer and parse it.

 USB GET_DESCRIPTOR request:
   bmRequestType = 0x81  (Device-to-Host, Standard, Interface)
   bRequest      = 0x06  (GET_DESCRIPTOR)
   wValue        = 0x2200 | interface_num (type=0x22 Report, index=intf)
   wIndex        = interface_number
   wLength       = descriptor_length (from HID class descriptor)

 IMPORTANT: This function is called from usb_client_task which owns the
 USB host client handle.  We MUST continue pumping
 usb_host_client_handle_events() while waiting for the control transfer
 callback — otherwise the USB host library never delivers the completion
 event and the transfer times out.  A simple xTaskNotifyWait() deadlocks.
------------------------------------------------------------------------- */

static volatile bool     s_ctrl_done   = false;
static volatile esp_err_t s_ctrl_status = ESP_FAIL;
static volatile uint32_t  s_ctrl_bytes  = 0;

static void ctrl_transfer_cb(usb_transfer_t *t)
{
    if (!t) return;
    s_ctrl_bytes  = (uint32_t)t->actual_num_bytes;
    s_ctrl_status = (t->status == USB_TRANSFER_STATUS_COMPLETED) ? ESP_OK : ESP_FAIL;
    s_ctrl_done   = true;   /* signal main loop — must be last write */
}

static esp_err_t fetch_and_parse_report_descriptor(void)
{
    if (s_dev == NULL || s_hid_rpt_len == 0 || s_hid_intf_num < 0) {
        ESP_LOGW(TAG, "fetch_report_desc: prerequisites not met");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t desc_len = s_hid_rpt_len;
    if (desc_len > HID_REPORT_DESC_MAX_BYTES) {
        ESP_LOGW(TAG, "Report descriptor length %u > max %u, clamping",
                 (unsigned)desc_len, HID_REPORT_DESC_MAX_BYTES);
        desc_len = HID_REPORT_DESC_MAX_BYTES;
    }

    /* Allocate transfer: 8 bytes SETUP + descriptor payload */
    usb_transfer_t *t = NULL;
    esp_err_t err = usb_host_transfer_alloc((size_t)(8u + desc_len), 0, &t);
    if (err != ESP_OK || !t) {
        ESP_LOGE(TAG, "usb_host_transfer_alloc for report desc: %s", esp_err_to_name(err));
        return err;
    }

    /* Build SETUP packet for GET_DESCRIPTOR (Report) */
    uint8_t *setup = t->data_buffer;
    setup[0] = 0x81u;                           /* bmRequestType: D→H, Standard, Interface */
    setup[1] = 0x06u;                           /* bRequest: GET_DESCRIPTOR */
    setup[2] = 0x00u;                           /* wValue low: descriptor index = 0 */
    setup[3] = USB_DESC_TYPE_HID_REPORT;        /* wValue high: descriptor type = 0x22 */
    setup[4] = (uint8_t)(s_hid_intf_num);       /* wIndex low: interface number */
    setup[5] = 0x00u;                           /* wIndex high */
    setup[6] = (uint8_t)(desc_len & 0xFFu);     /* wLength low */
    setup[7] = (uint8_t)(desc_len >> 8u);       /* wLength high */

    t->device_handle    = s_dev;
    t->bEndpointAddress = 0x00u;  /* control endpoint */
    t->callback         = ctrl_transfer_cb;
    t->context          = NULL;
    t->num_bytes        = (size_t)(8u + desc_len);

    /* Reset completion flag before submit */
    s_ctrl_done   = false;
    s_ctrl_status = ESP_FAIL;
    s_ctrl_bytes  = 0;

    err = usb_host_transfer_submit_control(s_client, t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_transfer_submit_control: %s", esp_err_to_name(err));
        usb_host_transfer_free(t);
        return err;
    }

    /* Pump the USB client event loop while waiting for completion.
     * This is required: the control-transfer callback is delivered via
     * usb_host_client_handle_events(), not via a hardware interrupt
     * directly to our task.  Blocking on xTaskNotifyWait() here would
     * starve the event pump and the callback would never fire.
     *
     * Timeout: 3 000 ms in 10 ms slices = 300 iterations. */
    const TickType_t slice_ticks = pdMS_TO_TICKS(10);
    const int        max_iter    = 300;   /* 3 000 ms total */
    int iter = 0;

    while (!s_ctrl_done && iter < max_iter) {
        /* Non-blocking poll: 0 ms timeout so we return immediately if
         * no event is pending.  The 10 ms delay below throttles the loop. */
        usb_host_client_handle_events(s_client, 0);
        vTaskDelay(slice_ticks);
        iter++;
    }

    if (!s_ctrl_done || s_ctrl_status != ESP_OK) {
        ESP_LOGE(TAG, "Report descriptor control transfer timed out or failed "
                 "(iter=%d done=%d status=%s)",
                 iter, (int)s_ctrl_done, esp_err_to_name(s_ctrl_status));
        usb_host_transfer_free(t);
        return ESP_FAIL;
    }

    /* Payload starts after the 8-byte SETUP packet */
    size_t actual_bytes = (size_t)s_ctrl_bytes;
    size_t payload_len  = (actual_bytes > 8u) ? (size_t)(actual_bytes - 8u) : 0u;
    const uint8_t *payload = t->data_buffer + 8u;

    ESP_LOGI(TAG, "HID Report Descriptor received: %u bytes (iter=%d)",
             (unsigned)payload_len, iter);

    /* Log the raw bytes at DEBUG level (useful for development) */
    {
        char hex[64 * 3 + 4];
        int  pos = 0;
        size_t log_n = payload_len > 64u ? 64u : payload_len;
        for (size_t i = 0; i < log_n; i++) {
            pos += snprintf(&hex[pos], sizeof(hex) - (size_t)pos,
                            "%02X ", payload[i]);
        }
        ESP_LOGD(TAG, "Report desc[0..%u]: %s%s",
                 (unsigned)(log_n > 0 ? log_n - 1 : 0), hex,
                 payload_len > 64u ? "..." : "");
    }

    if (payload_len == 0) {
        ESP_LOGE(TAG, "Empty report descriptor payload");
        usb_host_transfer_free(t);
        return ESP_FAIL;
    }

    /* Parse the descriptor */
    static hid_desc_t s_hid_desc;   /* static to avoid stack use */
    ups_hid_desc_init(&s_hid_desc);

    bool ok = ups_hid_desc_parse(payload, payload_len, &s_hid_desc);
    usb_host_transfer_free(t);

    if (!ok) {
        ESP_LOGE(TAG, "ups_hid_desc_parse failed — descriptor may be malformed");
        return ESP_FAIL;
    }

    /* Dump all fields at INFO level on first connection */
    ups_hid_desc_dump(&s_hid_desc);

    /* Give the parsed descriptor to the report decoder */
    ups_hid_parser_set_descriptor(&s_hid_desc);

    ESP_LOGI(TAG, "HID report descriptor parsed and loaded OK");
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 Interrupt IN reader
------------------------------------------------------------------------- */
#if UPS_USB_ENABLE_INTR_IN_READER

static void intr_in_cb(usb_transfer_t *t)
{
    if (!t) return;

    if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes > 0U) {
        const int     len = (int)t->actual_num_bytes;
        const uint8_t *d  = (const uint8_t *)t->data_buffer;

        bool changed = (len != s_last_in_len) ||
                       (memcmp(d, s_last_in, (size_t)len) != 0);

        if (changed) {
            s_last_in_len = (len > (int)sizeof(s_last_in)) ? (int)sizeof(s_last_in) : len;
            memcpy(s_last_in, d, (size_t)s_last_in_len);

            char line[64 * 3 + 1];
            int  pos = 0;
            int  n   = (len > 64) ? 64 : len;
            for (int i = 0; i < n; i++) {
                pos += snprintf(&line[pos], sizeof(line) - (size_t)pos,
                                "%02X%s", d[i], (i == n - 1) ? "" : " ");
            }
            ESP_LOGI(TAG, "HID IN changed (%d): %s", len, line);

            ups_state_update_t upd;
            if (ups_hid_parser_decode_report(d, (size_t)len, &upd)) {
                ups_state_apply_update(&upd);
            }
        }
    } else if (t->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "INT-IN transfer status=%d actual=%u",
                 (int)t->status, (unsigned)t->actual_num_bytes);
    }

    if (!s_dev_gone) {
        esp_err_t err = usb_host_transfer_submit(t);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "usb_host_transfer_submit(resubmit): %s", esp_err_to_name(err));
            usb_host_transfer_free(t);
        }
    } else {
        usb_host_transfer_free(t);
    }
}

static esp_err_t start_interrupt_in_reader(void)
{
    if (!s_dev || s_ep_in_addr == 0U || s_ep_in_mps == 0U)
        return ESP_ERR_INVALID_STATE;

    usb_transfer_t *t = NULL;
    esp_err_t err = usb_host_transfer_alloc(s_ep_in_mps, 0, &t);
    if (err != ESP_OK || !t) return err;

    t->device_handle    = s_dev;
    t->bEndpointAddress = s_ep_in_addr;
    t->callback         = intr_in_cb;
    t->context          = NULL;
    t->num_bytes        = s_ep_in_mps;

    ESP_LOGI(TAG, "Starting interrupt IN reader: EP=0x%02X MPS=%u",
             (unsigned)s_ep_in_addr, (unsigned)s_ep_in_mps);

    err = usb_host_transfer_submit(t);
    if (err != ESP_OK) usb_host_transfer_free(t);
    return err;
}

#endif /* UPS_USB_ENABLE_INTR_IN_READER */

/* -------------------------------------------------------------------------
 Main USB client task
------------------------------------------------------------------------- */
static void usb_client_task(void *arg)
{
    (void)arg;

    usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_cfg));
    ESP_LOGI(TAG, "usb_host_install OK");

    xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 20, NULL, 0);

    usb_host_client_config_t client_cfg = {
        .is_synchronous    = false,
        .max_num_event_msg = 8,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg          = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_cfg, &s_client));
    ESP_LOGI(TAG, "USB scan ready. Plug UPS into OTG port (VBUS must be powered).");

    while (1) {
        /* Use a short timeout so we can service the GET_REPORT queue
         * (ups_get_report_service_queue) even when no USB events are pending.
         * 10ms is negligible overhead; interrupt-IN callbacks are still
         * delivered promptly via the event mechanism. */
        (void)usb_host_client_handle_events(s_client, pdMS_TO_TICKS(10));

        /* Service any pending Feature Report GET_REPORT requests */
        ups_get_report_service_queue();

        if (s_dev_connected) {
            s_dev_connected = false;
            s_dev_gone      = false;
            reset_session();

            if (s_new_dev_addr == 0U) {
                ESP_LOGW(TAG, "NEW_DEV flagged but addr=0 — skip");
                continue;
            }

            usb_device_handle_t dev = NULL;
            esp_err_t err = usb_host_device_open(s_client, s_new_dev_addr, &dev);
            if (err != ESP_OK || !dev) {
                ESP_LOGE(TAG, "usb_host_device_open(addr=%u) failed: %s",
                         (unsigned)s_new_dev_addr, esp_err_to_name(err));
                continue;
            }

            s_dev = dev;
            ESP_LOGI(TAG, "Device opened (addr=%u)", (unsigned)s_new_dev_addr);

            /* Step 1: Read device descriptor (VID/PID) */
            log_device_descriptor(s_dev);

            /* Step 2: Load USB string descriptors */
            load_usb_strings(s_dev);

            /* Step 3: Parse configuration — find HID interface + endpoints */
            err = parse_active_config(s_dev);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "parse_active_config: %s", esp_err_to_name(err));
            }

            /* Step 4: Claim HID interface */
            if (s_hid_intf_num >= 0) {
                err = usb_host_interface_claim(s_client, s_dev,
                                               (uint8_t)s_hid_intf_num,
                                               (uint8_t)s_hid_alt);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Interface claimed (intf=%d alt=%d)",
                             s_hid_intf_num, s_hid_alt);
                } else {
                    ESP_LOGE(TAG, "usb_host_interface_claim failed: %s",
                             esp_err_to_name(err));
                }
            }

            /* Step 5: Fetch & parse HID Report Descriptor → loads parser.
             * NOTE: This pumps usb_host_client_handle_events() internally
             * while waiting, so the USB event loop stays alive. */
            if (s_hid_rpt_len > 0) {
                esp_err_t desc_err = fetch_and_parse_report_descriptor();
                if (desc_err != ESP_OK) {
                    ESP_LOGE(TAG, "fetch_and_parse_report_descriptor failed: %s — "
                             "interrupt reports will not be decoded",
                             esp_err_to_name(desc_err));
                }
            } else {
                ESP_LOGW(TAG, "HID report descriptor length is 0 — skipping fetch");
            }

            /* Step 6: Start interrupt IN reader */
#if UPS_USB_ENABLE_INTR_IN_READER
            if (s_ep_in_addr != 0U && s_ep_in_mps != 0U) {
                esp_err_t rerr = start_interrupt_in_reader();
                if (rerr != ESP_OK) {
                    ESP_LOGW(TAG, "start_interrupt_in_reader: %s",
                             esp_err_to_name(rerr));
                }
            } else {
                ESP_LOGW(TAG, "No INT-IN endpoint — skipping reader");
            }
#endif

            /* Step 7: Start GET_REPORT polling if device needs it */
            {
                uint16_t vid = s_vid, pid = s_pid;
                const ups_device_entry_t *entry = ups_device_db_lookup(vid, pid);
                if (entry && (entry->quirks & QUIRK_NEEDS_GET_REPORT)) {
                    ESP_LOGI(TAG, "Device has QUIRK_NEEDS_GET_REPORT — starting Feature report polling");
                    ups_get_report_start(s_client, s_dev, s_hid_intf_num, entry);
                } else {
                    ESP_LOGI(TAG, "No QUIRK_NEEDS_GET_REPORT — Feature report polling disabled");
                }
            }
        }

        if (s_dev_gone) {
            ESP_LOGW(TAG, "DEV_GONE — cleaning up");
            cleanup_device();
        }
    }
}

/* -------------------------------------------------------------------------
 Public entry point
------------------------------------------------------------------------- */
void ups_usb_hid_start(const app_cfg_t *cfg)
{
    (void)cfg;
    xTaskCreatePinnedToCore(usb_client_task, "ups_usb", 6144, NULL, 20, NULL, 0);
    ESP_LOGI(TAG, "ups_usb_hid module started");
}
