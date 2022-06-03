#pragma once

#include <stdbool.h>
#include <time.h>

typedef void (*discovergy_update_callback)(time_t time, float power, void *cb_arg);

bool discovergy_init();

void discovery_set_update_callback(discovergy_update_callback cb, void *cb_arg);