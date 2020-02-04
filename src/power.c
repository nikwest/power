#include "power.h"

#include "math.h"

#include "adc.h"
#include "battery.h"

#include "mgos.h"
#include "mgos_gpio.h"
#include "mgos_prometheus_metrics.h"
#include "mgos_crontab.h"

#define MAX_STEPS 64

static int current_power_in = 31;
static float total_power = 0.0;
static float capacity_in = 0.0;
static float capacity_out = 0.0;
static double last_capacity_update = 0.0;
static bool power_out_enabled = true;

static void power_metrics(struct mg_connection *nc, void *data) {
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "power_state", "State of power",
        "%d", power_get_state());
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "current_power_in", "State of current power in",
        "%d", current_power_in);
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "current_total_power", "State of current total power reported through mqtt",
        "%f", total_power);
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "optimize_power", "Optimizing power enabled",
        "%d", mgos_sys_config_get_power_optimize());
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "capacity_in", "charged amount in Ah",
        "%f", capacity_in);
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "capacity_out", "discharged amount in Ah",
        "%f", capacity_out);
   mgos_prometheus_metrics_printf(
        nc, GAUGE, "power_out_enabled", "power out enabled",
        "%d", power_out_enabled);

  (void) data;
}

static power_state_t power_update_capacity() {
  power_state_t state = power_get_state();
  double hours = last_capacity_update;
  last_capacity_update = mgos_uptime();
  hours = (last_capacity_update - hours) / 3600.0;
  switch (state) {
  case power_in:
    capacity_in += adc_read_power_in_current() * hours;
    break;
  case power_out:
    //capacity_out += adc_get_power_out() * hours;
    capacity_out += mgos_sys_config_get_power_out_current_max() * hours;
    break;
  default:
    break;
  }
  return state;
}

static void power_reset_capacity_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  LOG(LL_INFO, ("%.*s crontab job fired!", action.len, action.p));
  power_reset_capacity();

  (struct mg_str) payload;
  (void) userdata;
}

void power_init() {

    int in = mgos_sys_config_get_power_in_pin();
    int out = mgos_sys_config_get_power_out_pin();

    mgos_gpio_setup_output(in, false);
    mgos_gpio_setup_output(out, false);

    int ud = mgos_sys_config_get_power_in_power_ud_pin();
    int cs = mgos_sys_config_get_power_in_power_cs_pin();

    mgos_gpio_setup_output(ud, false);
    mgos_gpio_setup_output(cs, true);

    mgos_prometheus_metrics_add_handler(power_metrics, NULL);

    capacity_in = 0.0;
    capacity_out = 0.0;
    last_capacity_update = mgos_uptime();

    mgos_crontab_register_handler(mg_mk_str("power.reset_capacity"), power_reset_capacity_crontab_handler, NULL);
}


power_state_t power_get_state() {

    bool inval = mgos_gpio_read_out(mgos_sys_config_get_power_in_pin());
    bool outval = mgos_gpio_read_out(mgos_sys_config_get_power_out_pin());

    inval = !inval; // inverted logic

    if(!inval && !outval) {
        return power_off;
    }
    if(!inval && outval) {
        return power_out;
    }
    if(inval && !outval) {
        return power_in;
    }

    LOG(LL_ERROR, ("Invalid power state in: %d, out: %d", inval, outval));

    return power_invalid;
}

void power_set_state(power_state_t state) {
  power_update_capacity();
  int in = mgos_sys_config_get_power_in_pin();
  int out = mgos_sys_config_get_power_out_pin();
  battery_state_t battery_state = battery_get_state();

  switch (state) {
    case power_off:
      mgos_gpio_write(in, !false);
      mgos_gpio_write(out, false);
      if(battery_state == battery_charging || battery_state == battery_discharging) {
        battery_set_state(battery_idle);
      }
      break;
    case power_in:
      if(battery_state == battery_full || battery_state == battery_invalid) {
        LOG(LL_WARN, ("Invalid battery state %d", battery_state));
       break;
      }
      mgos_gpio_write(out, false);
      mgos_gpio_write(in, !true);
      battery_set_state(battery_charging);
      break;
    case power_out:
      if(!power_out_enabled) {
        LOG(LL_INFO, ("Power out disabled."));
        break;
      }
      if(battery_state == battery_empty || battery_state == battery_invalid) {
        LOG(LL_WARN, ("Invalid battery state %d", battery_state));
        break;
      }
      mgos_gpio_write(in, !false);
      mgos_gpio_write(out, true);
      battery_set_state(battery_discharging);
      break;
    default:
      LOG(LL_ERROR, ("Invalid power state %d", state));
      break;
  }
}

int power_in_change(int steps) {
  power_update_capacity();
  int ud = mgos_sys_config_get_power_in_power_ud_pin();
  int cs = mgos_sys_config_get_power_in_power_cs_pin();
  int s = abs(steps);
  // DW NOTE: timings only rough
  bool udstart = (steps > 0);
  mgos_gpio_write(ud, udstart);
  mgos_usleep(2);
  mgos_gpio_write(cs, false);
  mgos_usleep(2);
  while(s > 0) {
      mgos_gpio_write(ud, !udstart);
      mgos_usleep(2);
      mgos_gpio_write(ud, udstart);
      mgos_usleep(2);
      s--;
      current_power_in += (steps > 0) ? 1 : -1;
  }
  mgos_usleep(2);
  mgos_gpio_write(cs, true);

  current_power_in = (steps>0) ? MIN(current_power_in, MAX_STEPS) : MAX(current_power_in, 0);
  return current_power_in;
}

void power_set_total_power(float power) {
  total_power = power;
  if(mgos_sys_config_get_power_optimize()) {
    power_optimize(total_power);
  }
}

static float last_power = 0.0;

float power_optimize(float power) {
  power_state_t state = power_update_capacity();
  int target_min = mgos_sys_config_get_power_optimize_target_min();
  int target_max = mgos_sys_config_get_power_optimize_target_max();
  if(power > target_min && power < target_max) {
    return 0.0;
  }
  int target = (target_min + target_max) / 2;
  float battery_voltage = adc_read_battery_voltage();
  int num_cells = mgos_sys_config_get_battery_num_cells();
  float bv_max = mgos_sys_config_get_battery_cell_voltage_max() * num_cells;
  float bv_min = mgos_sys_config_get_battery_cell_voltage_min() * num_cells;
  float p_in_lsb = mgos_sys_config_get_power_in_lsb();
  float p_in = adc_get_power_in();
  
  if(power > target) {
    switch (state) {
      case power_off:
        if(MIN(power, last_power) > mgos_sys_config_get_power_out_min()) {
          power_set_state(power_out);
        }
        break;
      case power_in:
        if(p_in < MIN(power, last_power) ) {
          power_set_state(power_off);
        } else {
          int steps = (int) -power / p_in_lsb;
          power_in_change(steps);
        }
      default:
        break;
    }
  } else {
    switch (state) {
      case power_out:
        if(last_power < target) {
          power_set_state(power_off);
        }
        break;
      case power_off:
        if(last_power > target) {
          break;
        }
        power_set_state(power_in);
        // no break; also adjust power
      case power_in: 
        if(p_in < mgos_sys_config_get_power_in_max()) {
          int steps = ceil(fabs(power) / p_in_lsb);
          power_in_change(steps);
        } else {
          power_in_change(1);
        }
      default:
        break;
      }
  }
  last_power = power;
  return power;
}

void power_reset_capacity() {
  capacity_in = 0;
  capacity_out = 0;
}

void power_set_out_enabled(bool enabled) {
  power_out_enabled = enabled;
}

bool power_get_out_enabled() {
  return power_out_enabled;
}