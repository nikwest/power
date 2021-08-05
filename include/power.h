#pragma once

#include "mgos.h"


typedef enum  { 
    power_in = 1, 
    power_out = -1, 
    power_off = 0, 
    power_invalid = -99 
} power_state_t;

typedef enum  { 
    power_in_invalid = -99, // permanent
    power_in_failed = -98,  // temporary
    power_in_unknown = -97, 
    power_in_ok = 1, 
    power_in_no_change = 0, 
    power_in_at_min = -1, 
    power_in_at_max = 99 
} power_in_state_t;

void power_init();

static inline bool power_state_is_valid(power_state_t state) {
    return state == power_in || state == power_out || state == power_off;
}

power_state_t power_get_state();
void power_set_state(power_state_t state);

power_in_state_t power_in_change(float *power);

void power_set_total_power(float power);

float power_optimize(float power);
float power_optimize2(float power);

void power_reset_capacity();

void power_set_optimize_enabled(bool enabled);
bool power_get_optimize_enabled();

void power_set_out_enabled(bool enabled);
bool power_get_out_enabled();

void power_run_test();