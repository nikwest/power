#include "battery.h"

#include "mgos_ina219.h"
#include "mgos_prometheus_metrics.h"
#include "soyosource.h"


static struct mgos_ina219 *ina219 = NULL;
static battery_state_t state = battery_idle;
static int soc = 0;
static double last_state_change = 0;

// cell voltage in mV, normal temp, 0.5C, 0% - 100%
static const int soc_discharge[] = { 3100, 3120, 3160, 3190, 3200, 3210, 3220, 3240, 3260, 3270, 3340 };
//static int soc_charge[] =    { 3120, 3325, 3375, 3400, 3415, 3425, 3440, 3445, 3450, 3455, 3470 };
// adapted to 0.2C
static const int soc_charge[] =    { 3120, 3315, 3355, 3375, 3390, 3400, 3415, 3430, 3445, 3455, 3470 };
static const int soc_idle[] =      { 3110, 3223, 3263, 3295, 3308, 3318, 3330, 3343, 3355, 3363, 3405 };

static void battery_metrics_ina219(struct mg_connection *nc, void *data) {    
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

static void battery_cb_ina219(void *data) {
  struct mgos_ina219 *d = (struct mgos_ina219 *) data;
  if (!d) {
    LOG(LL_ERROR, ("ina219 device not available"));
    return;
  }
  float bus, shunt, current, res;

  mgos_ina219_get_bus_voltage(d, &bus);
  mgos_ina219_get_shunt_resistance(d, &res);
  mgos_ina219_get_shunt_voltage(d, &shunt);
  mgos_ina219_get_current(d, &current);
  LOG(LL_INFO, ("ina219: Vbus=%.3f V Vshunt=%.0f uV Rshunt=%.3f Ohm Ishunt=%.1f mA",
    bus, shunt*1e6, res, current*1e3));
}

static int battery_calculate_soc() {
  int cell_voltage = (battery_read_voltage() * 1000) / mgos_sys_config_get_battery_num_cells();
  const int *socs = NULL;
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
  if(!mgos_sys_config_get_battery_enabled()) {
    LOG(LL_WARN, ("Battery management disabled"));
    state = battery_disabled;
    return state;
  }
  switch (mgos_sys_config_get_battery_instrument()) {
  case 0:
    LOG(LL_WARN, ("No battery measurment instrument available, disabling battery management"));
    state = battery_disabled;
    return state;
    break;
  case 1:
    if (!(ina219 = mgos_ina219_create(mgos_i2c_get_global(), 0x40))) {
      LOG(LL_ERROR, ("Could not create INA219"));
      battery_set_state(battery_invalid);
      return state;
    }
    if(!mgos_ina219_set_shunt_resistance(ina219, 0.1)) {
      LOG(LL_ERROR, ("Could not set INA219 shunt resistance"));
      battery_set_state(battery_invalid);
      return state;
    }
    mgos_set_timer(1e4 /* ms */, MGOS_TIMER_REPEAT, battery_cb_ina219, ina219);
    mgos_prometheus_metrics_add_handler(battery_metrics_ina219, ina219);
    LOG(LL_INFO, ("Setup INA219"));
    break;
  case 2:
    if(!soyosource_get_enabled()) {
      LOG(LL_WARN, ("Soyosource is not enabled, disabling battery management"));
      battery_set_state(battery_invalid);
      return state;
    }
    LOG(LL_INFO, ("using soyosource"));
    break;
  default:
    LOG(LL_WARN, ("Unknown battery measurment instrument %d, disabling battery management", mgos_sys_config_get_battery_instrument()));
    state = battery_disabled;
    break;
  }

  battery_set_state(battery_idle);
  soc = battery_calculate_soc();
  return state;
}

battery_state_t battery_get_state() {
  return state;
}
void battery_set_state(battery_state_t s) {
  if(state == battery_disabled) {
    LOG(LL_WARN, ("Battery management disabled - cannot set state"));
    return;
  }
  state = s;
  last_state_change = mgos_uptime();
}

int battery_get_soc() {
  int interval = (state == battery_idle || state == battery_empty) 
    ? mgos_sys_config_get_battery_soc_settle_interval() : 30;

  if( (mgos_uptime() - last_state_change) > interval) {
    int new_soc = battery_calculate_soc();
    switch (state) {
    case battery_charging:
      soc = MAX(soc, new_soc);
      break;
    case battery_discharging:
      soc = MIN(soc, new_soc);
      break;
    default:
      soc = new_soc;
      break;
    }
  }
  return soc;
}

int battery_reset_soc() {
  soc = battery_calculate_soc();
  if(state == battery_empty || state == battery_full) {
    battery_set_state(battery_idle);
  }
  return soc;
}


float battery_read_voltage() {
  float result = 0.0;
  switch (mgos_sys_config_get_battery_instrument())
  {
  case 0:
    LOG(LL_ERROR, ("Could not read bus voltage, instrument disabled"));
    break;
  case 1:
    if(!mgos_ina219_get_bus_voltage(ina219, &result)) {
      LOG(LL_ERROR, ("Could not read bus voltage from INA219"));
    }
    break;
  case 2:
    result = soyosource_get_last_voltage();
    break;
  default:
    LOG(LL_ERROR, ("Could not read bus voltage, unknown instrument %d", mgos_sys_config_get_battery_instrument()));
    break;
  }
  return result;
}
float battery_read_current() {
  float result = 0.0;
  switch (mgos_sys_config_get_battery_instrument())
  {
  case 0:
    LOG(LL_ERROR, ("Could not read current, instrument disabled"));
    break;
  case 1:
    if(!  mgos_ina219_get_current(ina219, &result)) {
      LOG(LL_ERROR, ("Could not read bus voltage from INA219"));
    }
    break;
  case 2:
    result = soyosource_get_last_current();
    break;
  default:
    LOG(LL_ERROR, ("Could not read current, unknown instrument %d", mgos_sys_config_get_battery_instrument()));
    break;
  }
  return result;
}
