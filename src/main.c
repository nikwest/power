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

static void timer_cb(void *data) {
  static bool s_tick_tock = false;
  LOG(LL_INFO,
      ("%s uptime: %.2lf, RAM: %lu, %lu free", (s_tick_tock ? "Tick" : "Tock"),
       mgos_uptime(), (unsigned long) mgos_get_heap_size(),
       (unsigned long) mgos_get_free_heap_size()));
  s_tick_tock = !s_tick_tock;
  #ifdef LED_PIN
    mgos_gpio_toggle(LED_PIN);
  #endif

}

enum mgos_app_init_result mgos_app_init(void) {
  #ifdef LED_PIN
    mgos_gpio_setup_output(LED_PIN, 0);
  #endif

  mgos_set_timer(10000 /* ms */, MGOS_TIMER_REPEAT, timer_cb, NULL);

  adc_init();
  battery_init();
  power_init();
  rpc_init();
  watchdog_init();
  mqtt_init();

  return MGOS_APP_INIT_SUCCESS;
}
