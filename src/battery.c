#include "battery.h"

#include "mgos_ina219.h"
#include "mgos_prometheus_metrics.h"


static struct mgos_ina219 *ina219 = NULL;
static battery_state_t state = battery_invalid;

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

  (void) data;
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
  state = battery_ready;
  return state;
}

battery_state_t battery_get_state() {
  return state;
}
void battery_set_state(battery_state_t s) {
  state = s;
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
