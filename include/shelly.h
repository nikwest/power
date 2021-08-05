#pragma once

#include "mgos.h"

void shelly_init();

bool shelly_set_state(const char* destination, int id, bool state);
