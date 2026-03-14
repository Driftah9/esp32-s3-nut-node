# ESP32 UPS Full Modular Package (v14.8)

This package contains the real modularized network baseline plus the next USB UPS module step.

Included:
- main/            -> all source modules
- docs/            -> confirmed stable single-file main.c + updated source-of-truth log

## Module status
- cfg_store      : real
- wifi_mgr       : real
- http_portal    : real
- nut_server     : real
- ups_state      : real shared state pipe
- ups_usb_hid    : v14.8 scan/identify/claim + optional INT-IN logging

## Important build note
USB host will stay inactive unless you enable it in menuconfig / sdkconfig:

CONFIG_USB_HOST_ENABLED=y

## Build
idf.py fullclean
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor
