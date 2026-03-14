ESP32 UPS modular update pack v14.10 (WIP)

Purpose:
- extend parser toward the remaining NUT variables
- add cautious candidate mappings for:
  * battery.voltage
  * input.voltage
  * output.voltage
  * ups.load

Important:
- this is intentionally marked WIP
- candidate fields are gated by plausibility checks
- parser logs candidate mappings so you can validate values on both UPS models

What is already stable:
- ups.status
- battery.charge
- battery.runtime
- input.utility.present
- ups.flags

What this package tries to unlock:
- battery.voltage
- input.voltage
- output.voltage
- ups.load
