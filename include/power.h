#pragma once

#include "mgos.h"


typedef enum  { 
    power_in = 1, 
    power_out = -1, 
    power_off = 0, 
    power_invalid = -99 
} power_state_t;

void power_init();

inline bool power_state_is_valid(power_state_t state) {
    return state == power_in || state == power_out || state == power_off;
}

power_state_t power_get_state();
void power_set_state(power_state_t state);

int power_in_change(int steps);

void power_set_total_power(float power);

float power_optimize(float power);