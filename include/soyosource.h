#pragma once

#include <stdbool.h>

void soyosource_init();

void soyosource_set_power_out(int power);
int soyosource_get_power_out();

bool soyosource_get_enabled();
void soyosource_set_enabled(bool enabled);
void soyosource_request_status();

float soyosource_get_last_voltage();
float soyosource_get_last_current();
