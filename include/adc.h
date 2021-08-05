#pragma once

#include "mgos.h"

bool adc_init();

bool adc_available();

float adc_read_battery_voltage();
float adc_read_power_in_current();
float adc_read_power_out_current();
float adc_get_power_in();
float adc_get_power_out();
