#include "power.h"

#include "math.h"
#include "limits.h"
#include "mongoose.h"

#include "adc.h"
#include "battery.h"
#include "soyosource.h"

#include "mgos.h"
#include "mgos_gpio.h"
#include "mgos_rpc.h"
#include "mgos_prometheus_metrics.h"
#include "mgos_crontab.h"
#include "mgos_pwm.h"
#include "mgos_crontab.h"


#define PWM_FREQ 25000

static int current_power_in = 0;
static int current_power_out = 0;
static int current_steps_in = 0;
static int requested_steps_in = 0;
static float total_power = 0.0;
static float capacity_in = 0.0;
static float capacity_out = 0.0;
static double last_capacity_update = 0.0;
static bool power_optimize_enabled = false;
static bool power_out_enabled = true;
static float last_p_in_lsb = 0.0;
static int power_in_target = -1;
static int optimize_target_min = 0;
static int optimize_target_max = 0;

// DW FIX
static double battery_voltage = 0.0;


static power_change_impl in_impl = NULL;
static power_change_impl out_impl = NULL;

static double last_power_change = 0;

static struct {
  float items[8];
  int next;
  int size;
} power_pending;

static void power_metrics(struct mg_connection *nc, void *data) {
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "power_state", "State of power",
        "%d", power_get_state());
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "current_power_in", "State of current power in",
        "%d", current_power_in);
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "current_power_out", "State of current power out",
        "%d", current_power_out);
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "requested_steps_in", "Requesgted change of poti for input power",
        "%d", requested_steps_in);
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "current_steps_in", "State of poti for input power",
        "%d", current_steps_in);
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
    //capacity_in += adc_read_power_in_current() * hours;
    capacity_in += current_power_in / battery_voltage * hours;
    break;
  case power_out:
    //capacity_out += adc_get_power_out() * hours;
    capacity_out += current_power_out / battery_voltage * hours;
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

  (void) payload;
  (void) userdata;
}

static power_change_state_t power_in_change_mcp4021(float* power) {
  int ud = mgos_sys_config_get_power_in_power_ud_pin();
  int cs = mgos_sys_config_get_power_in_power_cs_pin();
  int s = (int) fabs(*power);
  // TODO: max/min limits
  // DW NOTE: timings only rough
  bool udstart = (*power > 0);
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
  }
  mgos_usleep(2);
  mgos_gpio_write(cs, true);
  
  return power_change_ok;
}

static power_change_state_t power_in_change_max5389(float* power) {
  float p_in_lsb = mgos_sys_config_get_power_in_lsb();
  int steps = (int) (*power / p_in_lsb);
  int ud = mgos_sys_config_get_power_in_power_ud_pin();
  int dir = mgos_sys_config_get_power_in_power_cs_pin();
  int s = (int) fabs(steps);
  // asuming CS is enabled
  // DW TODO: min/max limits
  // DW NOTE: timings only rough
  bool udstart = (steps < 0);
  mgos_gpio_write(dir, udstart);
  mgos_usleep(1);
  while(s > 0) {
      mgos_gpio_write(ud, true);
      mgos_usleep(1);
      mgos_gpio_write(ud, false);
      mgos_usleep(1);
      s--;
  }

  return power_change_ok;
}

static power_change_state_t power_in_change_dummy(float* power) {
  if(total_power > power_get_optimize_target_max()) {
    return power_change_at_min;
  } else if(total_power > power_get_optimize_target_min()) {
    return power_change_at_max;
  }
  return power_change_no_change;
}

static power_change_state_t power_in_set_pwm(float duty) {
  int pin = mgos_sys_config_get_power_in_power_ud_pin();
  power_change_state_t result = power_change_invalid;
  if(duty == 0) {
    mgos_pwm_set(pin, 0, 0); // can fail if no pwm has been started
    mgos_gpio_write(pin, false);
    result = power_change_at_min;
  } else if(duty == 1.0) {
    mgos_pwm_set(pin, 0, 0); // can fail if no pwm has been started
    mgos_gpio_write(pin, true);
    result = power_change_at_max;
  } else {
    // inverted
    if(!mgos_pwm_set(pin, PWM_FREQ, 1.0 - duty)) {
      LOG(LL_ERROR, ("Updating PWM to %f failed", duty));
      return power_change_failed;
    }
    result = power_change_ok;
  }
  return result;
}

static power_change_state_t power_in_change_pwm(float* power) {
  if(*power == 0) {
    return power_change_no_change;
  } 
  int max_power = mgos_sys_config_get_power_in_max();
  //int min_power = mgos_sys_config_get_power_in_min();
  if(max_power == 0) {
    LOG(LL_ERROR, ("MAX Power setting required for PWM"));
    return power_change_invalid;
  }
  float damping = mgos_sys_config_get_power_in_damping();
  float duty = (float) (current_power_in + *power * damping) / max_power;
  duty = fmin(1.0, fmax( 0.0, duty));

  power_change_state_t result = power_in_set_pwm(duty);
  
  current_steps_in = duty * 100;
  float new_power_in = duty * max_power;

  LOG(LL_INFO, ("Updating PWM to %f [New: %.2fW, Previous: %dW, Asked: %.2f]", duty, new_power_in, current_power_in, *power));

  *power = new_power_in - current_power_in;
  return result;
}

static power_change_state_t power_in_change_drv8825(float* power) {
  float p_in_lsb = mgos_sys_config_get_power_in_lsb();
  int steps = (int) *power / p_in_lsb;
  int max_steps = mgos_sys_config_get_power_steps();
  int ud = mgos_sys_config_get_power_in_power_ud_pin();
  int dir = mgos_sys_config_get_power_in_power_cs_pin();
  int delay = mgos_sys_config_get_power_stepper_delay();
  int s = abs(steps);
  // asuming CS is enabled
  // DW NOTE: timings only rough
  bool udstart = (steps > 0);
  requested_steps_in = steps;
  if(steps < 0 && current_steps_in == 0) {
    return power_change_at_min;
  } else if(steps > 0 && current_steps_in == max_steps) {
    return power_change_at_max;
  } else if(current_steps_in + steps < 0) {
    steps = -current_steps_in;
    LOG(LL_WARN, ("At min step after stepping %d", steps));
  } else if(current_steps_in + steps > max_steps) {
    steps = max_steps - current_steps_in;
    LOG(LL_WARN, ("At max step after stepping %d", steps));
  }

  mgos_gpio_write(dir, udstart);
  mgos_usleep(100);
  while(s > 0 /* TODO && current_power_in < mgos_sys_config_get_power_steps() && current_power_in >= 0 */) {
      mgos_gpio_write(ud, false);
      mgos_usleep(delay);
      mgos_gpio_write(ud, true);
      mgos_usleep(delay);
      s--;
      current_power_in += (steps > 0) ? 1 : -1;
  }
  current_steps_in += steps;
  return power_change_ok;
}


static void power_in_change_rpc_cb(struct mg_rpc *c, void *cb_arg,
                               struct mg_rpc_frame_info *fi,
                               struct mg_str result, int error_code,
                               struct mg_str error_msg) {
  if(error_code) {
    LOG(LL_ERROR, ("power_in_change_rpc_cb error: %d %.*s", error_code, error_msg.len, error_msg.p)); 
    return;
  } 

  LOG(LL_INFO, ("power_in_change_rpc_cb: %.*s", result.len, result.p));  
}

static power_change_state_t power_in_change_rpc(float* power) {
  struct mg_rpc *c = mgos_rpc_get_global();
  struct mg_rpc_call_opts opts = {
    .dst = mg_mk_str(mgos_sys_config_get_power_in_slave()) 
  };
  if(!mg_rpc_callf(c, mg_mk_str("Power.InChange"), power_in_change_rpc_cb, NULL, &opts,
             "{power: %d}", power)) {
    LOG(LL_ERROR, ("power_in_change_rpc: calling %.*s failed.", opts.dst.len, opts.dst.p));  
    return power_change_failed;                          
  }
  LOG(LL_ERROR, ("power_in_change_rpc: called."));  
  return power_change_unknown;
}

static power_change_state_t apply_in_limits(float* power) {
  int min = mgos_sys_config_get_power_in_min();
  int max = mgos_sys_config_get_power_in_max();

  // check if disabled
  if(max <= min) {
    return power_change_ok;
  }

  // if(power < (min - max)) {
  //   // better switch off
  //   return 0;
  // }

  if(power_in_target >= 0) {
    if(current_power_in + *power > power_in_target) {
      *power = power_in_target - current_power_in;
      LOG(LL_INFO, ("Correcting power in to %f to power in target %d", *power,  power_in_target));
    } else {
      LOG(LL_INFO, ("Power in target %d not reached %d", power_in_target, current_power_in));
    }
  }

  if(current_power_in <= min && *power < 0) {
    LOG(LL_INFO, ("Power in at Min, current: %d, asked: %.2f", current_power_in, *power));
    return power_change_at_min;
  } else if(current_power_in >= max && *power > 0) {
    LOG(LL_INFO, ("Power in at Max, current: %d, asked: %.2f", current_power_in, *power));
    return power_change_at_max;
  }


  if(adc_available()) {
    float power_in = adc_get_power_in();
    if(power_in + *power < min) {
      *power = min - power_in;
    } else if(power_in + *power > max) {
      *power = max - power_in;
    }
  }

  return power_change_ok;
}


static power_change_state_t apply_out_limits(float* power) {
  float min = (float) mgos_sys_config_get_power_out_min();
  float max = (float) mgos_sys_config_get_power_out_max();

  // check if disabled
  if(max <= min ) { //|| !adc_available()
    LOG(LL_INFO, ("Out limits disabled"));
    return power_change_ok;
  }

  // if(power < (min - max)) {
  //   // better switch off
  //   return 0;
  // }
  if(current_power_out <= min && *power < 0) {
    LOG(LL_INFO, ("Power out at Min, current: %d, asked: %.2f", current_power_out, *power));
    return power_change_at_min;
  } else if(current_power_out >= max && *power > 0) {
    LOG(LL_INFO, ("Power out at Max, current: %d, asked: %.2f", current_power_out, *power));
    return power_change_at_max;
  } 

  float power_out = (float) current_power_out; // TODO: adc_get_power_out();
  if(power_out + *power < min) {
    *power = min - power_out;
  } else if(power_out + *power > max) {
    *power = max - power_out;
  }

   LOG(LL_INFO, ("current_power_out: %d, changing by: %.2f", current_power_out, *power));
 
  return power_change_ok;
}


static power_change_state_t power_out_change_soyosource(float* power) {
  soyosource_set_enabled(true);
  //current_power_out = soyosource_get_power_out(); // not up to date, when serial reading is slow
  if(*power == 0) {
    return power_change_no_change;
  } 
  
  int max_power = mgos_sys_config_get_power_out_max();
  int min_power = mgos_sys_config_get_power_out_min();
  float damping = mgos_sys_config_get_power_out_damping();
  if(current_power_out <= min_power && *power < 0) {
    LOG(LL_INFO, ("Out power at minimum %d [requested: %f] - switching off", current_power_out , *power));
    *power = 0;
    soyosource_set_power_out(0);
    return power_change_at_min;
  }

  power_change_state_t result = power_change_ok;
  int p = (*power > 0) ? (*power * damping) : *power;
  int new_power_out = MIN(max_power, MAX( min_power, current_power_out + p));
  
  soyosource_set_power_out(new_power_out);
  *power = new_power_out - current_power_out;
  LOG(LL_INFO, ("Changed out power from %d to %d [requested: %.2f]", current_power_out, new_power_out, *power));

  return result;
}

static power_change_state_t power_enable_tps2121(float* power) {
  // TODO: ????
  int ud = mgos_sys_config_get_power_in_power_ud_pin();
  power_state_t state = power_get_state();
  power_change_state_t result = power_change_invalid;
  switch (state) {
  case power_out:
    mgos_gpio_write(ud, true);
    result = power_change_at_max;
    break;
  case power_in:
    mgos_gpio_write(ud, false);
    result = power_change_at_min;
    break;
  default:
    result = power_change_no_change;
  }
  LOG(LL_INFO, ("Power state: %d, change state: %d, power: %.2f", state, result, *power));
  return result;
}

static power_change_impl power_get_change_impl(power_change_driver_t type) {
  power_change_impl impl = NULL;
  switch (type) {
  case power_change_dummy:
    impl = power_in_change_dummy;
    break;
  case power_change_pwm:
    impl = power_in_change_pwm;
    break;
  case power_change_mcp4021:
    impl = power_in_change_mcp4021;
    break;
  case power_change_max5389:
    impl = power_in_change_max5389;
    break;
  case power_change_drv8825:
    impl = power_in_change_drv8825;
    break;
  case power_change_rpc:
    impl = power_in_change_rpc;
    break;
  case power_change_soyosource:
    impl = power_out_change_soyosource;
    break;
  case power_change_tps2121:
    impl = power_enable_tps2121;
    break;

  default:
    LOG(LL_ERROR, ("Unknown power change driver %d, using dummy driver", type));
    impl = power_in_change_dummy;
    break;
  }
  return impl;
}

static void power_get_status() {
  int status = mgos_sys_config_get_power_status_pin();
  if(status != -1) {
    LOG(LL_INFO, ("TPS2121 status: %d", mgos_gpio_read(status)));
  }
}

void power_init() {
    power_pending.size = mgos_sys_config_get_power_pending_count();

    int in_driver = mgos_sys_config_get_power_in_change_driver();
    int out_driver = mgos_sys_config_get_power_out_change_driver();
    in_impl = power_get_change_impl(in_driver);
    out_impl = power_get_change_impl(out_driver);

    if(in_driver == power_change_pwm) {
      power_in_set_pwm(0);
    }

    last_p_in_lsb = mgos_sys_config_get_power_in_lsb();

    int in = mgos_sys_config_get_power_in_pin();
    int out = mgos_sys_config_get_power_out_pin();

    power_optimize_enabled = (bool) mgos_sys_config_get_power_optimize();

    mgos_gpio_setup_output(in, false);
    mgos_gpio_setup_output(out, false);

    int ud = mgos_sys_config_get_power_in_power_ud_pin();
    if(ud != -1) {
      mgos_gpio_setup_output(ud, false);
    }
    int cs = mgos_sys_config_get_power_in_power_cs_pin();
    if(cs != -1) {
      mgos_gpio_setup_output(cs, true);
    }

    int status = mgos_sys_config_get_power_status_pin();
    if(status != -1) {
      mgos_gpio_setup_input(status, MGOS_GPIO_PULL_UP);
    }

    optimize_target_min = mgos_sys_config_get_power_optimize_target_min();
    optimize_target_max = mgos_sys_config_get_power_optimize_target_max();

    mgos_prometheus_metrics_add_handler(power_metrics, NULL);

    capacity_in = 0.0;
    capacity_out = 0.0;
    last_capacity_update = mgos_uptime();
    current_steps_in = mgos_sys_config_get_power_steps() / 2; // TODO: arbitrary start
    battery_voltage = mgos_sys_config_get_battery_num_cells() * (mgos_sys_config_get_battery_cell_voltage_min() + mgos_sys_config_get_battery_cell_voltage_max()) / 2.0;

    mgos_crontab_register_handler(mg_mk_str("power.reset_capacity"), power_reset_capacity_crontab_handler, NULL);
    power_set_state(power_off);
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

  LOG(LL_INFO, ("Set power state to %d", state));

  switch (state) {
    case power_off:
      mgos_gpio_write(in, !false);
      mgos_gpio_write(out, false);
      if(battery_state == battery_charging || battery_state == battery_discharging) {
        battery_set_state(battery_idle);
      }
      current_power_out = 0;
      current_power_in = 0;
      if(soyosource_get_enabled()) {
        soyosource_set_power_out(0);
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

power_change_state_t power_in_change(float* power) {
  if(power_get_state() != power_in) {
    LOG(LL_INFO, ("Cannot change power in, not in state power_in"));
    return power_change_invalid;
  }
  power_update_capacity();
  power_change_state_t result = apply_in_limits(power);

  if(result != power_change_ok) { 
    return result; 
  }

  result = in_impl(power);
  if(result != 0) {
    last_power_change = mg_time();
  }

  // result = power_in_change_pwm(power);

  // const char* slave = mgos_sys_config_get_power_in_slave();
  // if(slave == NULL) {
  //   result = power_in_change_dummy(power);
  // } else {
  //   LOG(LL_INFO, ("power_in_change_rpc: calling slave %s.", slave));  
  //   result = power_in_change_rpc(power);
  // }
  return result;
}

power_change_state_t power_out_change(float* power) {
  if(power_get_state() != power_out) {
    LOG(LL_INFO, ("Cannot change power out, not in state power_out"));
    return power_change_invalid;
  }
  power_update_capacity();
  power_change_state_t result = apply_out_limits(power);

  if(result != power_change_ok) { 
    return result; 
  }

  result = out_impl(power);
  if(result != 0) {
    last_power_change = mg_time();
  }
  return result;
}

void power_set_total_power(float power) {
  total_power = power;
  if(power_get_optimize_enabled()) {
    power_optimize(total_power);
  }
}

void power_set_optimize_enabled(bool enabled) {
  power_optimize_enabled = enabled;
}

bool power_get_optimize_enabled() {
  return power_optimize_enabled;
}

float power_optimize(float power) {
  power_state_t state = power_update_capacity();
  int target_min = power_get_optimize_target_min();
  int target_max = power_get_optimize_target_max();
  int target_mid = (target_max + target_min) / 2;
  int in_min = mgos_sys_config_get_power_in_min();
  //float p_in = adc_get_power_in();
  int i = 0;

  float pp = power;
  for(i=0; i<power_pending.size; i++) {
     pp += power_pending.items[i];
     LOG(LL_INFO, ("Pending[%d]: %.2f", i, power_pending.items[i])); 
  }
  LOG(LL_INFO, ("PP: %.2f", pp));

  float pending = pp;

  i = power_pending.next;
  float p = pending - target_mid;
  switch (state) {
    case power_off:
      if(pending < target_min && pending < (target_mid - in_min) ) {
        p = -p;
        power_set_state(power_in);
        if(power_in_change(&p) !=  power_change_at_min) {
          current_power_in = (int) p;
        }
      }
      else if(pending > mgos_sys_config_get_power_out_on()) {
        power_set_state(power_out);
        if(power_out_change(&p) !=  power_change_at_min) {
          current_power_out = (int) p;
        }
      } else {
        p = 0;
      }
      break;
    case power_in:
      if(pending < target_min || pending > target_max) {
        //if(adc_get_power_in() < mgos_sys_config_get_power_in_min() ) {
        p = -p;
        if(power_in_change(&p) ==  power_change_at_min) {
          power_set_state(power_off);
        } else {
          current_power_in += (int) p;
        }
      } else {
        //current_power_in -= (int) power; // optimize to 0
        p = 0;
      }
      break;
      // if(p_in < p_diff) {
      //   power_in_change(-1);
      //   last_p_in_lsb = fabs(p_in - adc_get_power_in());
      //   power_set_state(power_off);
      //   break;
      // } 
      // while(fabs(p_diff) > last_p_in_lsb) {
      //   power_in_change((p_diff > 0) - (p_diff < 0));
      //   last_p_in_lsb = fabs(p_in - adc_get_power_in()); // !!! 
      // }
    case power_out:
      if(!power_get_out_enabled()) {
        power_set_state(power_off);
        p = 0;
        break;
      }
      if(pending < target_min || pending > target_max) {
        //if(adc_get_power_in() < mgos_sys_config_get_power_in_min() ) {
        if(power_out_change(&p) ==  power_change_at_min) {
          power_set_state(power_off);
        } else {
          current_power_out += (int) p;
        }
      } else {
        p = 0;
      }
      if(current_power_out <= mgos_sys_config_get_power_out_off()) {
        power_set_state(power_off); 
        p = 0;
      } 
      break;
    default:
      LOG(LL_WARN, ("Unexpected state: %d", state));
      p = 0;
      break;
  }
  if(power_pending.size > 0) {
    power_pending.items[i++] = p;
    power_pending.next = i % power_pending.size; 
    LOG(LL_INFO, ("p: %.2f\tcurrent_power_out %d\tpending %.2f", p, current_power_out, pending));
  }
  return p;
}

void power_reset_capacity() {
  capacity_in = 0;
  capacity_out = 0;
}

void power_set_out_enabled(bool enabled) {
  power_out_enabled = enabled;
  soyosource_set_enabled(enabled);
}

bool power_get_out_enabled() {
  return power_out_enabled;
}

void power_set_optimize_target_min(int min) {
  optimize_target_min = min;
}

int power_get_optimize_target_min() {
  return optimize_target_min;
}

void power_set_optimize_target_max(int max) {
  optimize_target_max = max;
}

int power_get_optimize_target_max() {
  return optimize_target_max;
}

void power_set_in_target(int target) {
  power_in_target = target;
}
int power_get_in_target() {
  return power_in_target;
}

double power_get_last_power_change() {
  return last_power_change;
}



static void power_run_test_handler(void *arg) {
  power_run_test();
  (void) arg;
}

void power_run_test() {
  static int i = 127;
  static float p = 1;
  if(i == mgos_sys_config_get_power_steps()) {
    p = -1.0;
  } else if(i == 0) {
    p = 1.0;
  }
  LOG(LL_INFO, ("Step: %d", i));
  power_in_change_max5389(&p);
  i += p;
  mgos_set_timer(1000 /* ms */, 0, power_run_test_handler, NULL);
}