#pragma once

#include <stdbool.h>

typedef void (*discovergy_update_callback)(double time, float power, void *cb_arg);

bool discovergy_init();

void discovery_set_update_callback(discovergy_update_callback cb, void *cb_arg);

// 0 if none
double discovery_get_last_update();