#pragma once

#include "mgos.h"

#define DEFAULT_PRICE_LIMIT (-1.0)

bool watchdog_init();


void watchdog_set_power_optimize(bool enabled);
bool watchdog_get_power_optimize();

bool watchdog_evaluate_power_out(float limit, float *price);