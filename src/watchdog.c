#include "watchdog.h"

#include "mgos.h"
#include "mgos_dash.h"
#include "battery.h"
#include "power.h"
#include "discovergy.h"

static void watchdog_handler(void *data) {
  float battery_min = mgos_sys_config_get_power_battery_voltage_min();
  float battery_max = mgos_sys_config_get_power_battery_voltage_max();
  float battery = battery_read_voltage();
  power_state_t state = power_get_state();
  battery_state_t battery_state = battery_get_state();

  if(state == power_in && battery > battery_max) {
    power_set_state(power_off);
    battery_set_state(battery_full);
  } else if(state == power_out && battery < battery_min) {
    power_set_state(power_off);
    battery_set_state(battery_empty);
  }
  state = power_get_state();
  LOG(LL_INFO, ("power_state: %d\n battery_voltage: %f\n", state, battery));

  mgos_dash_notifyf(
    "Status", 
    "{power_state: %d, battery_state: %d, battery_voltage: %f}", 
    state, battery_state, battery
  );
}

static void discovergy_handler(time_t update, float power, void* cb_arg) {
  char time[20];
  mgos_strftime(time, 32, "%x %X", update);
  LOG(LL_INFO, ("%s: %.2f", time, power));
  power_set_total_power(power);
}

void watchdog_init() {

  if( mgos_sys_config_get_power_watchdog_enable() ) {
    int interval = mgos_sys_config_get_power_watchdog_interval();
    mgos_set_timer(interval * 1000 /* ms */, MGOS_TIMER_REPEAT, watchdog_handler, NULL);
  }

  discovery_set_update_callback(discovergy_handler, NULL);
}
