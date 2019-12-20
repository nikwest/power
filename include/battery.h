#pragma once

#include "mgos.h"


typedef enum  { 
    battery_empty = -1, 
    battery_full = 1, 
    battery_ready = 0, 
    battery_invalid = -99 
} battery_state_t;

battery_state_t battery_init();

inline bool battery_state_is_valid(battery_state_t state) {
    return state == battery_empty || state == battery_full || state == battery_ready;
}

battery_state_t battery_get_state();
void battery_set_state(battery_state_t state);


float battery_read_voltage();
float battery_read_current();
