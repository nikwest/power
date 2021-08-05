#include "shelly.h"

#include "mgos.h"
#include "mgos_rpc.h"



void shelly_init() {

}

static void shelly_set_state_cb(struct mg_rpc *c, void *cb_arg,
                               struct mg_rpc_frame_info *fi,
                               struct mg_str result, int error_code,
                               struct mg_str error_msg) {
  if(error_code) {
    LOG(LL_ERROR, ("shelly_set_state_cb error: %d %.*s", error_code, error_msg.len, error_msg.p)); 
    return;
  } 

  LOG(LL_INFO, ("shelly_set_state_cb: %.*s", result.len, result.p));  
}

//curl http://shelly1-test.local/rpc/Shelly.SetState -d '{"id": 1, "type": 0, "state": {"state": true}}'
bool shelly_set_state(const char* destination, int id, bool state) {
  struct mg_rpc *c = mgos_rpc_get_global();
  struct mg_rpc_call_opts opts = {
    .dst = mg_mk_str(destination) 
  };
  if(!mg_rpc_callf(c, mg_mk_str("Shelly.SetState"), shelly_set_state_cb, NULL, &opts,
             "{id: %d, type: 0, state: {state: %B}}", id, state)) {
    LOG(LL_ERROR, ("shelly_set_state: calling %.*s failed.", opts.dst.len, opts.dst.p));  
    return false;                          
  }
  return true;
}
