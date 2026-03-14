ESP32 UPS modular update pack v14.9

Included updates:
- ups_usb_hid.c / .h
  * parser integration
  * reattach recovery
  * feeds decoded reports into ups_state
- ups_hid_parser.c / .h
  * decodes currently confirmed APC HID reports
- ups_state.c / .h
  * expanded metric/state model
- nut_server.c
  * exposes expanded variables when valid
- CMakeLists.txt
  * adds ups_hid_parser.c

Notes:
- Current confirmed parsed variables:
  * ups.status
  * battery.charge
  * battery.runtime
  * input.utility.present
  * ups.flags
- Added state/NUT plumbing for:
  * battery.voltage
  * input.voltage
  * output.voltage
  * ups.load
  These will appear once parser mappings are added for those metrics.
