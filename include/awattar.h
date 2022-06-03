#pragma once

#include <stdbool.h>
#include <time.h>

typedef struct {
  time_t start;
  time_t end;
  float price;
} awattar_pricing_t;

typedef void (*awattar_update_callback)(awattar_pricing_t *entries, int length, void *cb_arg);

bool awattar_init();

void awattar_set_update_callback(awattar_update_callback cb, void *cb_arg);

int awattar_get_entries_count();
awattar_pricing_t* awattar_get_entries();
awattar_pricing_t* awattar_get_entry(time_t time);
awattar_pricing_t* awattar_get_best_entry(time_t after);
