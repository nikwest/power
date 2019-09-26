#include "power.h"

#include "mgos.h"
#include "mgos_gpio.h"
#include "mgos_prometheus_metrics.h"

#define MAX_STEPS 64

static int current_power_in = 31;

static void power_metrics(struct mg_connection *nc, void *data) {
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "power_state", "State of power",
        "%d", power_get_state());
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "current_power_in", "State of current power in",
        "%d", current_power_in);
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

    int in = mgos_sys_config_get_power_in_pin();
    int out = mgos_sys_config_get_power_out_pin();

    switch (state) {
    case power_off:
        mgos_gpio_write(in, !false);
        mgos_gpio_write(out, false);
        break;
   case power_in:
        mgos_gpio_write(out, false);
        mgos_gpio_write(in, !true);
        break;
   case power_out:
        mgos_gpio_write(in, !false);
        mgos_gpio_write(out, true);
        break;
    default:
        LOG(LL_ERROR, ("Invalid power state %d", state));
        break;
    }
}

int power_in_change(int steps) {
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


