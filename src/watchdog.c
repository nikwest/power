#include "watchdog.h"

#include "mgos.h"
#include "mgos_dash.h"
#include "adc.h"
#include "power.h"


static void watchdog_handler(void *data) {
  float battery_min = mgos_sys_config_get_power_battery_voltage_min();
  float battery_max = mgos_sys_config_get_power_battery_voltage_max();
  float battery = adc_read_battery_voltage();
  float in = adc_read_power_in_current();
  float out = adc_read_power_out_current();
  power_state_t state = power_get_state();

  if(
      (state == power_in && battery > battery_max) ||
      (state == power_out && battery < battery_min)
    ) {
    power_set_state(power_off);
    state = power_get_state();
    mgos_sys_config_set_power_optimize(false);
  }
  LOG(LL_INFO, ("power_state: %d\n battery_voltage: %f\n power_in_current: %f\n power_out_current: %f\n", state, battery, power_in, power_out));

  mgos_dash_notifyf(
    "Status", 
    "{power_state: %d, battery_voltage: %f, power_in_current: %f, power_out_current: %f}", 
    state, battery, in, out
  );
}

void watchdog_init() {

  if( mgos_sys_config_get_power_watchdog_enable() ) {
    int interval = mgos_sys_config_get_power_watchdog_interval();
    mgos_set_timer(interval * 1000 /* ms */, MGOS_TIMER_REPEAT, watchdog_handler, NULL);
  }

}
