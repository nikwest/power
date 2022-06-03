#pragma once

#include <stdbool.h>


typedef enum  { 
    battery_empty = -2, 
    battery_discharging = -1,
    battery_idle = 0, 
    battery_charging = 1, 
    battery_full = 2, 
    battery_disabled = -98, 
    battery_invalid = -99 
} battery_state_t;

battery_state_t battery_init();

inline bool battery_state_is_valid(battery_state_t state) {
    return state == battery_empty 
    || state == battery_full 
    || state == battery_charging 
    || state == battery_discharging 
    || state == battery_empty 
    || state == battery_idle
    || state == battery_disabled;
}

battery_state_t battery_get_state();
void battery_set_state(battery_state_t state);

int battery_get_soc();
int battery_reset_soc();
float battery_read_voltage();
float battery_read_current();
