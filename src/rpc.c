#include "rpc.h"

#include "common/mg_str.h"
#include "mgos.h"
#include "mgos_rpc.h"

#include "power.h"
#include "battery.h"
#include "watchdog.h"
#include "fan.h"


static void rpc_log(struct mg_rpc_request_info *ri, struct mg_str args) {
  if(ri->src.len == 0) {
    LOG(LL_INFO,
      ("tag=%.*s src=NULL method=%.*s args='%.*s'", ri->tag.len, ri->tag.p,
       ri->method.len, ri->method.p, args.len, args.p));
  } else {
    LOG(LL_INFO,
      ("tag=%.*s src=%.*s method=%.*s args='%.*s'", ri->tag.len, ri->tag.p,
       ri->src.len, ri->src.p, ri->method.len, ri->method.p, args.len, args.p));
  }
  // TODO(pim): log to MQTT
}

static void rpc_power_get_handler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  power_state_t state;

  rpc_log(ri, args);

  state = power_get_state();
  mg_rpc_send_responsef(ri, "{state: %d}", state);
  ri = NULL;

  (void) cb_arg;
  (void) fi;
}

static void rpc_power_set_handler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  power_state_t state;

  rpc_log(ri, args);

  if (1 != json_scanf(args.p, args.len, ri->args_fmt, &state)) {
    mg_rpc_send_errorf(ri, 400, "state is a required argument");
    ri = NULL;
    return;
  }

  if (!power_state_is_valid(state)) {
    mg_rpc_send_errorf(ri, 400, "state must be power_in (%d), power_out (%d) or power_off (%d)",
                       power_in, power_out, power_off);
    ri = NULL;
    return;
  }
  if(state == power_out) {
    power_set_out_enabled(true);
  }
  power_set_state(state);

  state = power_get_state();
  mg_rpc_send_responsef(ri, "{state: %d}", state);
  ri = NULL;

  (void) cb_arg;
  (void) fi;
}

static void rpc_power_in_change_handler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  float power;

  rpc_log(ri, args);

  if (1 != json_scanf(args.p, args.len, ri->args_fmt, &power)) {
    mg_rpc_send_errorf(ri, 400, "power is a required argument");
    ri = NULL;
    return;
  }

  power_in_change(&power);

  mg_rpc_send_responsef(ri, "{power: %.2f}", power);
  ri = NULL;

  (void) cb_arg;
  (void) fi;
}

static void rpc_power_out_change_handler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  float power;

  rpc_log(ri, args);

  if (1 != json_scanf(args.p, args.len, ri->args_fmt, &power)) {
    mg_rpc_send_errorf(ri, 400, "power is a required argument");
    ri = NULL;
    return;
  }

  power_out_change(&power);

  mg_rpc_send_responsef(ri, "{power: %.2f}", power);
  ri = NULL;

  (void) cb_arg;
  (void) fi;
}


static void rpc_battery_soc_reset(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  rpc_log(ri, args);
  mg_rpc_send_responsef(ri, "{soc: %d}", battery_reset_soc());

  (void) cb_arg;
  (void) fi;
}

static void rpc_power_out_evaluate(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  rpc_log(ri, args);

  float price = -1.0;
  float limit = DEFAULT_PRICE_LIMIT;
  json_scanf(args.p, args.len, ri->args_fmt, &limit);
  mg_rpc_send_responsef(ri, "{enabled: %B, price: %f}", watchdog_evaluate_power_out(limit, &price), price);

  (void) cb_arg;
  (void) fi;
}

static void rpc_power_set_in_target(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  rpc_log(ri, args);

  int target = 0;
  json_scanf(args.p, args.len, ri->args_fmt, &target);
  power_set_in_target(target);
  mg_rpc_send_responsef(ri, "{target: %d}", power_get_in_target());

  (void) cb_arg;
  (void) fi;
}

static void rpc_power_set_enable_optimize(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  rpc_log(ri, args);
  int enabled;
  if (1 != json_scanf(args.p, args.len, ri->args_fmt, &enabled)) {
    mg_rpc_send_errorf(ri, 400, "enable is a required argument");
    ri = NULL;
    return;
  }
  power_set_optimize_enabled(enabled);
  mg_rpc_send_responsef(ri, "{enable: %B}", power_get_optimize_enabled());

  (void) cb_arg;
  (void) fi;
}

static void rpc_power_set_optimize_target(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  int min = power_get_optimize_target_min();
  int max = power_get_optimize_target_max();
  json_scanf(args.p, args.len, ri->args_fmt, &min, &max);
  power_set_optimize_target_min(min);
  power_set_optimize_target_max(max);
  mg_rpc_send_responsef(ri, "{min: %d, max: %d}", power_get_optimize_target_min(), power_get_optimize_target_max());

  (void) cb_arg;
  (void) fi;
}

static void rpc_watchdog_set_measure_lag(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  int power = 0;
  if (1 != json_scanf(args.p, args.len, ri->args_fmt, &power)) {
    mg_rpc_send_errorf(ri, 400, "power is a required argument");
    ri = NULL;
    return;
  }  
  watchdog_measure_lag(power);
  mg_rpc_send_responsef(ri, "{power: %d}", power);

  (void) cb_arg;
  (void) fi;
}

static void rpc_fan_speed_handler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  int percent;

  rpc_log(ri, args);

  if (1 != json_scanf(args.p, args.len, ri->args_fmt, &percent)) {
    mg_rpc_send_errorf(ri, 400, "percent is a required argument");
    ri = NULL;
    return;
  }

  fan_set_speeds(percent);

  mg_rpc_send_responsef(ri, "{percent: %d}", percent);
  ri = NULL;
}

void rpc_init() {
  struct mg_rpc *c = mgos_rpc_get_global();

  mg_rpc_add_handler(c, "Power.GetState", "", rpc_power_get_handler,
                     NULL);
  mg_rpc_add_handler(c, "Power.SetState", "{state: %d}",
                     rpc_power_set_handler, NULL);
  mg_rpc_add_handler(c, "Power.InChange", "{power: %f}",
                     rpc_power_in_change_handler, NULL);
  mg_rpc_add_handler(c, "Power.OutChange", "{power: %f}",
                     rpc_power_out_change_handler, NULL);
  mg_rpc_add_handler(c, "Power.ResetSOC", "",
                     rpc_battery_soc_reset, NULL);
  mg_rpc_add_handler(c, "Power.OutEvaluate", "{limit: %f}",
                     rpc_power_out_evaluate, NULL);
  mg_rpc_add_handler(c, "Power.Optimize", "{enable: %B}",
                     rpc_power_set_enable_optimize, NULL);
  mg_rpc_add_handler(c, "Power.SetInTarget", "{target: %d}",
                     rpc_power_set_in_target, NULL);
  mg_rpc_add_handler(c, "Power.SetOptimizeTarget", "{min: %d, max: %d}",
                     rpc_power_set_optimize_target, NULL);
  mg_rpc_add_handler(c, "Watchdog.MeasureLag", "{power: %d}",
                     rpc_watchdog_set_measure_lag, NULL);
  mg_rpc_add_handler(c, "Fan.Speed", "{percent: %d}",
                     rpc_fan_speed_handler, NULL);
                    
}

