#pragma once

#include <stdbool.h>

typedef enum  { 
    power_in = 1, 
    power_out = -1, 
    power_off = 0, 
    power_invalid = -99 
} power_state_t;

typedef enum  { 
    power_change_invalid = -99, // permanent
    power_change_failed = -98,  // temporary
    power_change_unknown = -97, 
    power_change_ok = 1, 
    power_change_no_change = 0, 
    power_change_at_min = -1, 
    power_change_at_max = 99 
} power_change_state_t;

typedef power_change_state_t (*power_change_impl)(float *power);

typedef enum {
    power_change_dummy = 0,
    power_change_pwm = 1,
    power_change_mcp4021 = 2,
    power_change_max5389 = 3,
    power_change_drv8825 = 4,
    power_change_rpc = 5,
    power_change_soyosource = 6,
    power_change_tps2121 = 7
} power_change_driver_t;

void power_init();

static inline bool power_state_is_valid(power_state_t state) {
    return state == power_in || state == power_out || state == power_off;
}

power_state_t power_get_state();
void power_set_state(power_state_t state);

power_change_state_t power_in_change(float *power);
power_change_state_t power_out_change(float* power);

void power_set_total_power(float power);

float power_optimize(float power);
float power_optimize2(float power);

void power_reset_capacity();

void power_set_optimize_enabled(bool enabled);
bool power_get_optimize_enabled();

void power_set_optimize_target_max(int max);
int power_get_optimize_target_max();
void power_set_optimize_target_min(int min);
int power_get_optimize_target_min();

void power_set_out_enabled(bool enabled);
bool power_get_out_enabled();

void power_set_in_target(int target);
int power_get_in_target();

// 0 if none
double power_get_last_power_change();

void power_run_test();