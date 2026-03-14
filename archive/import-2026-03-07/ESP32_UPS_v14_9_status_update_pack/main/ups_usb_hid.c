/*============================================================================
 MODULE: ups_usb_hid

 RESPONSIBILITY
 - USB Host lifecycle
 - Detect NEW_DEV/DEV_GONE
 - Log VID:PID, parse HID interface, HID descriptor length, IN endpoint
 - Start interrupt-IN change-only reader
 - Recover cleanly on unplug so reattach can be detected
 - Feed decoded HID packets into ups_state

 REVERT HISTORY
 R0  v14.7 USB skeleton placeholder
 R1  v14.8 scan + identify + claim + INT-IN logging
 R2  v14.8.1 remove false-disabled compile-time stub
 R3  v14.8.2 reattach recovery cleanup path
 R4  v14.9 parser + ups_state integration

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
#include "usb/usb_helpers.h"

#include "ups_hid_parser.h"
#include "ups_state.h"

static const char *TAG = "ups_usb_hid";

/* USB descriptor types (HID) */
#define USB_DESC_TYPE_HID        0x21
#define USB_DESC_TYPE_HID_REPORT 0x22

#ifndef UPS_USB_ENABLE_REPORT_DESC_DUMP
#define UPS_USB_ENABLE_REPORT_DESC_DUMP 0
#endif

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

static uint8_t  s_last_in[64];
static int      s_last_in_len = -1;

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
    ups_hid_parser_reset();
}

static void cleanup_device(void)
{
    if (s_dev != NULL) {
        if (s_hid_intf_num >= 0) {
            esp_err_t rel = usb_host_interface_release(s_client, s_dev, (uint8_t)s_hid_intf_num);
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

    reset_session();

    ESP_LOGI(TAG, "USB device cleanup complete. Waiting for NEW_DEV...");
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    (void)arg;
    if (event_msg == NULL) return;

    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        s_new_dev_addr  = event_msg->new_dev.address;
        s_dev_connected = true;
        ESP_EARLY_LOGI(TAG, "NEW_DEV received: addr=%u", (unsigned)s_new_dev_addr);
    } else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        s_dev_gone = true;
        ESP_EARLY_LOGW(TAG, "DEV_GONE received");
    }
}

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

    ESP_LOGI(TAG, "USB Device: VID:PID=%04x:%04x bcdUSB=%04x class=%02x sub=%02x proto=%02x",
             (unsigned)dd->idVendor, (unsigned)dd->idProduct, (unsigned)dd->bcdUSB,
             (unsigned)dd->bDeviceClass, (unsigned)dd->bDeviceSubClass, (unsigned)dd->bDeviceProtocol);

    ESP_LOGI(TAG, "iMfr=%u iProduct=%u iSerial=%u cfg=%u",
             (unsigned)dd->iManufacturer, (unsigned)dd->iProduct,
             (unsigned)dd->iSerialNumber, (unsigned)dd->bNumConfigurations);

    // Best-effort identity; ESP-IDF host API does not expose cached string fetch here.
    ups_state_set_usb_identity(s_vid, s_pid, s_hid_rpt_len, "UNKNOWN", "UNKNOWN", "UNKNOWN");
}

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

    const uint8_t *p = (const uint8_t *)cfg;
    const uint16_t total = cfg->wTotalLength;

    bool in_hid_intf = false;
    s_hid_intf_num = -1;

    for (uint16_t i = 0; i + 2U <= total;) {
        const uint8_t bLength = p[i];
        const uint8_t bType   = p[i + 1];

        if (bLength == 0U) {
            ESP_LOGW(TAG, "Descriptor parse: bLength=0 at i=%u (abort)", (unsigned)i);
            break;
        }
        if ((uint16_t)(i + bLength) > total) {
            ESP_LOGW(TAG, "Descriptor parse: overrun at i=%u len=%u total=%u (abort)",
                     (unsigned)i, (unsigned)bLength, (unsigned)total);
            break;
        }

        if (bType == USB_B_DESCRIPTOR_TYPE_INTERFACE && bLength >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)&p[i];
            in_hid_intf = (intf->bInterfaceClass == USB_CLASS_HID);
            if (in_hid_intf) {
                s_hid_intf_num = intf->bInterfaceNumber;
                s_hid_alt      = intf->bAlternateSetting;
                ESP_LOGI(TAG, "HID interface: intf=%u alt=%u sub=%02x proto=%02x",
                         (unsigned)s_hid_intf_num, (unsigned)s_hid_alt,
                         (unsigned)intf->bInterfaceSubClass, (unsigned)intf->bInterfaceProtocol);
            }
        } else if (bType == USB_DESC_TYPE_HID && in_hid_intf && bLength >= sizeof(usb_hid_desc_t)) {
            const usb_hid_desc_t *hid = (const usb_hid_desc_t *)&p[i];
            s_hid_rpt_len = hid->wReportDescriptorLength;
            ESP_LOGI(TAG, "HID Report Descriptor length = %u bytes (from HID 0x21)",
                     (unsigned)s_hid_rpt_len);
            ups_state_set_usb_identity(s_vid, s_pid, s_hid_rpt_len, "UNKNOWN", "UNKNOWN", "UNKNOWN");
        } else if (bType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && in_hid_intf && bLength >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)&p[i];
            const uint8_t xfer_type = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK);
            const bool is_in = (ep->bEndpointAddress & 0x80U) != 0U;

            if (is_in && xfer_type == USB_BM_ATTRIBUTES_XFER_INT) {
                s_ep_in_addr = ep->bEndpointAddress;
                s_ep_in_mps  = ep->wMaxPacketSize;
                ESP_LOGI(TAG, "Interrupt IN EP=0x%02x MPS=%u interval=%u",
                         (unsigned)s_ep_in_addr, (unsigned)s_ep_in_mps, (unsigned)ep->bInterval);
            }
        }

        i = (uint16_t)(i + bLength);
    }

    if (s_hid_intf_num < 0) {
        ESP_LOGW(TAG, "No HID interface found in active config");
        return ESP_ERR_NOT_FOUND;
    }
    if (s_ep_in_addr == 0U || s_ep_in_mps == 0U) {
        ESP_LOGW(TAG, "HID interface found but no interrupt IN endpoint detected");
    }
    return ESP_OK;
}

#if UPS_USB_ENABLE_REPORT_DESC_DUMP
static void maybe_dump_report_descriptor(usb_device_handle_t dev)
{
    if (s_hid_intf_num < 0 || s_hid_rpt_len == 0) {
        ESP_LOGW(TAG, "Skip report desc dump (missing hid_intf or length)");
        return;
    }

    const uint16_t want = (s_hid_rpt_len > 4096U) ? 4096U : s_hid_rpt_len;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(want, MALLOC_CAP_DEFAULT);
    if (buf == NULL) {
        ESP_LOGE(TAG, "malloc(%u) failed for report descriptor", (unsigned)want);
        return;
    }

    int actual = 0;
    esp_err_t err = usb_control_get_descriptor(
        dev,
        0x81, 0x06,
        (uint16_t)((USB_DESC_TYPE_HID_REPORT << 8) | 0x00),
        (uint16_t)s_hid_intf_num,
        buf, (int)want,
        &actual
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET_DESCRIPTOR(report 0x22) failed: %s", esp_err_to_name(err));
        heap_caps_free(buf);
        return;
    }

    ESP_LOGI(TAG, "HID Report Descriptor received: %d bytes", actual);
    heap_caps_free(buf);
}
#else
static void maybe_dump_report_descriptor(usb_device_handle_t dev)
{
    (void)dev;
}
#endif

#if UPS_USB_ENABLE_INTR_IN_READER
static void intr_in_cb(usb_transfer_t *t)
{
    if (t == NULL) return;

    if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes > 0U) {
        const int len = (int)t->actual_num_bytes;
        const uint8_t *d = (const uint8_t *)t->data_buffer;

        bool changed = (len != s_last_in_len) ||
                       (memcmp(d, s_last_in, (size_t)len) != 0);

        if (changed) {
            s_last_in_len = (len > (int)sizeof(s_last_in)) ? (int)sizeof(s_last_in) : len;
            memcpy(s_last_in, d, (size_t)s_last_in_len);

            char line[64 * 3 + 1];
            int pos = 0;
            int n = (len > 64) ? 64 : len;
            for (int i = 0; i < n; i++) {
                pos += snprintf(&line[pos], sizeof(line) - (size_t)pos, "%02X%s",
                                d[i], (i == n - 1) ? "" : " ");
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
    if (s_dev == NULL || s_ep_in_addr == 0U || s_ep_in_mps == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    usb_transfer_t *t = NULL;
    esp_err_t err = usb_host_transfer_alloc(s_ep_in_mps, 0, &t);
    if (err != ESP_OK || t == NULL) {
        return err;
    }

    t->device_handle    = s_dev;
    t->bEndpointAddress = s_ep_in_addr;
    t->callback         = intr_in_cb;
    t->context          = NULL;
    t->num_bytes        = s_ep_in_mps;

    ESP_LOGI(TAG, "Starting interrupt IN reader: EP=0x%02x MPS=%u (change-only)",
             (unsigned)s_ep_in_addr, (unsigned)s_ep_in_mps);

    err = usb_host_transfer_submit(t);
    if (err != ESP_OK) {
        usb_host_transfer_free(t);
    }
    return err;
}
#endif

static void usb_client_task(void *arg)
{
    (void)arg;

    usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_cfg));
    ESP_LOGI(TAG, "usb_host_install OK");

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
    ESP_LOGI(TAG, "USB scan ready. Plug UPS into OTG port now (VBUS must be powered).");

    while (1) {
        (void)usb_host_client_handle_events(s_client, portMAX_DELAY);

        if (s_dev_connected) {
            s_dev_connected = false;
            s_dev_gone = false;
            reset_session();

            if (s_new_dev_addr == 0U) {
                ESP_LOGW(TAG, "NEW_DEV flagged but addr=0? skip");
                continue;
            }

            usb_device_handle_t dev = NULL;
            esp_err_t err = usb_host_device_open(s_client, s_new_dev_addr, &dev);
            if (err != ESP_OK || dev == NULL) {
                ESP_LOGE(TAG, "usb_host_device_open(addr=%u) failed: %s",
                         (unsigned)s_new_dev_addr, esp_err_to_name(err));
                continue;
            }

            s_dev = dev;
            ESP_LOGI(TAG, "Device opened (addr=%u)", (unsigned)s_new_dev_addr);
            log_device_descriptor(s_dev);

            err = parse_active_config(s_dev);
            if (err == ESP_OK) {
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

                maybe_dump_report_descriptor(s_dev);

#if UPS_USB_ENABLE_INTR_IN_READER
                if (s_ep_in_addr != 0U && s_ep_in_mps != 0U) {
                    esp_err_t rerr = start_interrupt_in_reader();
                    if (rerr != ESP_OK) {
                        ESP_LOGW(TAG, "start_interrupt_in_reader: %s", esp_err_to_name(rerr));
                    }
                } else {
                    ESP_LOGW(TAG, "No INT-IN endpoint; skipping reader");
                }
#endif
            } else {
                ESP_LOGW(TAG, "parse_active_config: %s", esp_err_to_name(err));
            }
        }

        if (s_dev_gone) {
            ESP_LOGW(TAG, "DEV_GONE received");
            cleanup_device();
        }
    }
}

void ups_usb_hid_start(const app_cfg_t *cfg)
{
    (void)cfg;
    xTaskCreatePinnedToCore(usb_client_task, "ups_usb", 6144, NULL, 20, NULL, 0);
    ESP_LOGI(TAG, "ups_usb_hid module started (scan+identify)");
}
