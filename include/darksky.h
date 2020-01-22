#pragma once

#include "mgos.h"

typedef struct {
  time_t time;
  time_t sunrise;
  time_t sunset;
  float clouds;
} darksky_day_forecast_t;

typedef void (*darksky_update_callback)(darksky_day_forecast_t *entries, int length, void *cb_arg);

bool darksky_init();

void darksky_set_update_callback(darksky_update_callback cb, void *cb_arg);

