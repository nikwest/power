#include "battery.h"

#include "mgos_ina219.h"
#include "mgos_prometheus_metrics.h"


static struct mgos_ina219 *ina219 = NULL;
static battery_state_t state = battery_invalid;
static int soc = 0;
static long last_state_change = 0;

// cell voltage in mV, normal temp, 0.5C, 0% - 100%
static int soc_discharge[] = { 3100, 3120, 3160, 3190, 3200, 3210, 3220, 3240, 3260, 3270, 3340 };
static int soc_charge[] =    { 3050, 3325, 3375, 3400, 3415, 3425, 3440, 3445, 3450, 3455, 3470 };
static int soc_idle[] =      { 3075, 3223, 3263, 3295, 3308, 3318, 3330, 3343, 3355, 3363, 3405 };

static void battery_metrics(struct mg_connection *nc, void *data) {
    struct mgos_ads1x1x *d = (struct mgos_ads1x1x *)data;
    
    float result = battery_read_current();
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "battery_out_current", "Current out (Ampere)",
        "{type=\"ina219\", unit=\"0\"} %f", result);

    result = battery_read_voltage();
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "battery_voltage", "Battery Voltage (Volt)",
        "{type=\"ina219\", unit=\"0\"} %f", result);

    mgos_prometheus_metrics_printf(
        nc, GAUGE, "battery_state", "Battery State",
        "%d", battery_get_state());

    mgos_prometheus_metrics_printf(
        nc, GAUGE, "battery_soc", "Battery State of Charge in percent",
        "%d", battery_get_soc());

  (void) data;
}

static int battery_calculate_soc() {
  int cell_voltage = (battery_read_voltage() * 1000) / mgos_sys_config_get_power_battery_num_cells();
  int *socs = NULL;
  switch (state) {
  case battery_empty:
  case battery_idle:
  case battery_full:
    socs = soc_idle;
    break;
  case battery_charging:
    socs = soc_charge;
    break;
  case battery_discharging:
    socs = soc_discharge;
    break;
  default:
    break;
  }
  if(socs == NULL) {
    return soc;
  }
  int i = 0;
  
  while(i < 11 && socs[i] < cell_voltage) {
    i++;
  }
  int d = (i > 0) ? (((cell_voltage - socs[i-1]) * 10) / (socs[i] - socs[i-1])) : 0;
  return (i * 10) + d;
}


battery_state_t battery_init() {
  if (!(ina219 = mgos_ina219_create(mgos_i2c_get_global(), 0x40))) {
    LOG(LL_ERROR, ("Could not create INA219"));
    state = battery_invalid;
    return state;
  }
  LOG(LL_INFO, ("Setup INA219"));

  //mgos_set_timer(10000 /* ms */, MGOS_TIMER_REPEAT, battery_cb, ina219);

  mgos_prometheus_metrics_add_handler(battery_metrics, ina219);
  battery_set_state(battery_idle);
  soc = battery_calculate_soc();
  return state;
}

battery_state_t battery_get_state() {
  return state;
}
void battery_set_state(battery_state_t s) {
  state = s;
  last_state_change = mgos_uptime();
}

int battery_get_soc() {
  int interval = (state == battery_charging || state == battery_discharging) 
    ? mgos_sys_config_get_power_battery_soc_settle_interval() 
    : 100 * mgos_sys_config_get_power_battery_soc_settle_interval();
  if( (mgos_uptime() - last_state_change) > interval) {
    soc = battery_calculate_soc();
  }
  return soc;
}

float battery_read_voltage() {
  float result = 0.0;
  if(!mgos_ina219_get_bus_voltage(ina219, &result)) {
    LOG(LL_ERROR, ("Could not read bus voltage from INA219"));
  }
  return result;
}
float battery_read_current() {
  float result = 0.0;
  if(!mgos_ina219_get_current(ina219, &result)) {
    LOG(LL_ERROR, ("Could not read current from INA219"));
  }
  return result;
}
