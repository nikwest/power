#pragma once

#include "mgos.h"

typedef struct {
  time_t start;
  time_t end;
  float price;
} stockprice_t;


bool awattar_init();
