/* MODULE ups_state | REVERT R0 v14.7 usb skeleton */
#include "ups_state.h"
#include <string.h>
void ups_state_init(ups_state_t *s){memset(s,0,sizeof(*s));}
void ups_state_set_demo_defaults(ups_state_t *s){s->battery_charge=99;s->battery_runtime_s=35640;s->input_utility_present=1;s->ups_flags=0xD;strcpy(s->ups_status,"OL");}
