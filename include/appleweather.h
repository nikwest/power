#pragma once

#include "mgos.h"

typedef struct {
  time_t time;
  time_t sunrise;
  time_t sunset;
  float clouds;
} appleweather_day_forecast_t;

typedef void (*appleweather_update_callback)(appleweather_day_forecast_t *entries, int length, void *cb_arg);

bool appleweather_init();

void appleweather_set_update_callback(appleweather_update_callback cb, void *cb_arg);

