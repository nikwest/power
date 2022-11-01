#pragma once

#include "mgos.h"


bool fan_init();
void fan_set_speeds(int s);
void fan_set_speed(int fan, int s);