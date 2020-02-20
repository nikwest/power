#pragma once

#include "mgos.h"

#define DEFAULT_PRICE_LIMIT (-1.0)

bool watchdog_init();


bool watchdog_evaluate_power_out(float limit, float *price);