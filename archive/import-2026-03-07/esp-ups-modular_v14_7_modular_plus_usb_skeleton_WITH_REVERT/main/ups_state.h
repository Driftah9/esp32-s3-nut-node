/* MODULE ups_state | REVERT R0 v14.7 usb skeleton */
#pragma once
#include <stdint.h>
typedef struct{uint8_t battery_charge;uint32_t battery_runtime_s;int input_utility_present;unsigned ups_flags;char ups_status[8];int valid;} ups_state_t;
void ups_state_init(ups_state_t*);
void ups_state_set_demo_defaults(ups_state_t*);
