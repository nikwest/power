#include "rpc.h"

#include "common/mg_str.h"
#include "mgos.h"
#include "mgos_rpc.h"

#include "power.h"
#include "battery.h"

static void rpc_log(struct mg_rpc_request_info *ri, struct mg_str args) {
  LOG(LL_INFO,
      ("tag=%.*s src=%.*s method=%.*s args='%.*s'", ri->tag.len, ri->tag.p,
       ri->src.len, ri->src.p, ri->method.len, ri->method.p, args.len, args.p));
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
  int steps;

  rpc_log(ri, args);

  if (1 != json_scanf(args.p, args.len, ri->args_fmt, &steps)) {
    mg_rpc_send_errorf(ri, 400, "steps is a required argument");
    ri = NULL;
    return;
  }

  steps = power_in_change(steps);

  mg_rpc_send_responsef(ri, "{steps: %d}", steps);
  ri = NULL;

  (void) cb_arg;
  (void) fi;
}

static void rpc_battery_soc_reset(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  rpc_log(ri, args);
  mg_rpc_send_responsef(ri, "{soc: %d}", battery_reset_soc());
}

void rpc_init() {
  struct mg_rpc *c = mgos_rpc_get_global();

  mg_rpc_add_handler(c, "Power.GetState", "", rpc_power_get_handler,
                     NULL);
  mg_rpc_add_handler(c, "Power.SetState", "{state: %d}",
                     rpc_power_set_handler, NULL);
  mg_rpc_add_handler(c, "Power.InChange", "{steps: %d}",
                     rpc_power_in_change_handler, NULL);
  mg_rpc_add_handler(c, "Power.ResetSOC", "",
                     rpc_battery_soc_reset, NULL);

}
