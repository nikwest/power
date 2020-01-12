#pragma once

#include "mgos.h"



typedef void (*update_callback)(time_t time, float power, void *cb_arg);

bool discovergy_init();

void discovery_set_update_callback(update_callback cb, void *cb_arg);