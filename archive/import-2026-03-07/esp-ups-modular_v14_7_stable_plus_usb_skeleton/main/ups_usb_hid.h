#pragma once
#include "cfg_store.h"
#include "ups_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// APC UPS USB VID:PID (Back-UPS family commonly 051d:0002)
#define UPS_USB_VID 0x051d
#define UPS_USB_PID 0x0002

// Initialize the USB UPS (HID) subsystem.
// Non-blocking: starts background task(s).
void ups_usb_hid_start(const app_cfg_t *cfg);

// Optional: returns true if USB stack is enabled in this build (menuconfig)
bool ups_usb_hid_is_enabled(void);

#ifdef __cplusplus
}
#endif
