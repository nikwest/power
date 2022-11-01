/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mgos.h"
#include "mgos_config.h"
#include "mgos_rpc.h"

#include "power.h"
#include "rpc.h"
#include "adc.h"
#include "battery.h"
#include "watchdog.h"
#include "mqtt.h"
#include "discovergy.h"
#include "awattar.h"
#include "darksky.h"
#include "shelly.h"
#include "soyosource.h"
#include "ds18xxx.h"
#include "fan.h"


enum mgos_app_init_result mgos_app_init(void) {
  
  ds18xxx_init();
  fan_init();
  adc_init();
  soyosource_init();
  battery_init();
  power_init();
  rpc_init();
  mqtt_init();
  discovergy_init();
  awattar_init();
  //darksky_init();
  shelly_init();
  watchdog_init();

  //power_run_test();

  return MGOS_APP_INIT_SUCCESS;
}
