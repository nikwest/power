#include "watchdog.h"

#include "mgos.h"
#include "mgos_dash.h"
#include "mgos_cron.h"
#include "mgos_crontab.h"
#include "mgos_prometheus_metrics.h"
#include <time.h>

#include "battery.h"
#include "power.h"
#include "awattar.h"
#include "discovergy.h"
#include "darksky.h"
#include <math.h>


#define PRICE_INVALID -1.0

static const float monthly_radiation[] = { 30, 45, 80, 125, 160, 165, 165, 140, 95, 60, 30, 25 };
static const float performance_ratio = 0.75;
static int estimated_yield = 0;
static float price_avg = 0;
static float price_sigma = 0;
static float price_current = 0;


static int estimate_yield(darksky_day_forecast_t forecast) {
  int peak = mgos_sys_config_get_solar_peak_power();
  int base = mgos_sys_config_get_power_out_min();
  struct tm *t = localtime(forecast.time);
  float sunshine =  (forecast.sunset - forecast.sunrise) / 3600.0;
  sunshine *= (1.0 - forecast.clouds);
  float r = monthly_radiation[t->tm_mon] / 30000.0 * performance_ratio;
  float yield = ((peak * r) - (base * sunshine));
  LOG(LL_INFO, ("Estimated yield: %.0f", yield));
  return yield;
}

static void watchdog_metrics(struct mg_connection *nc, void *data) {
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "estimated_yield", "Estimated daily yield in Wh",
        "%d", estimate_yield
    );
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "awattar_current_price", "Current awattar price in EUR/kWh ",
        "%f", price_current
    );
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "awattar_avg_price", "24h average of awattar price in EUR/kWh ",
        "%f", price_avg
    );
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "awattar_sigma_price", "24h sigam of awattar price in EUR/kWh ",
        "%f", price_sigma
    );
}

static void watchdog_handler(void *data) {
  float battery_min = mgos_sys_config_get_battery_voltage_min();
  float battery_max = mgos_sys_config_get_battery_voltage_max();
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

static void watchdog_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  LOG(LL_INFO, ("%.*s crontab job fired!", action.len, action.p));
  watchdog_handler(userdata);
}

static void power_out_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  price_current = 0.0;                      
  if(price_sigma == PRICE_INVALID) {
    return;
  }
  time_t now = time(NULL);                 
  awattar_pricing_t* current = awattar_get_entry(now);
  if(current == NULL) {
    return;
  }
  price_current = current->price;
  power_set_out_enabled( (price_current > price_avg + price_sigma) );
}

static void discovergy_handler(time_t update, float power, void* cb_arg) {
  // char time[20];
  // mgos_strftime(time, 32, "%x %X", update);
  // LOG(LL_INFO, ("%s: %.2f", time, power));
  power_set_total_power(power);
}

static void darksky_handler(darksky_day_forecast_t *entries, int length, void *cb_arg) {
  if(length == 0) {
    LOG(LL_INFO, ("no weather data available"));
    return;
  }
  estimated_yield = estimate_yield(entries[0]);
}

static void awattar_handler(awattar_pricing_t *entries, int length, void *cb_arg) {
  price_avg = 0.0;
  price_sigma = 0.0;

  if(entries == NULL || length == 0) {
    price_sigma = PRICE_INVALID; // invalid
  }
  for(int i = 0; i<length; i++) {
    price_avg += entries[i].price;
  }
  price_avg /= length;
  for(int i = 0; i<length; i++) {
    price_sigma += (entries[i].price - price_avg) * (entries[i].price - price_avg);
  }
  price_sigma /= length;
  price_sigma = sqrtf(price_sigma);
}


bool watchdog_init() {
  mgos_prometheus_metrics_add_handler(watchdog_metrics, NULL);
  discovery_set_update_callback(discovergy_handler, NULL);
  darksky_set_update_callback(darksky_handler, NULL);
  awattar_set_update_callback(awattar_handler, NULL);
  mgos_crontab_register_handler(mg_mk_str("watchdog"), watchdog_crontab_handler, NULL);
  mgos_crontab_register_handler(mg_mk_str("power_out"), power_out_crontab_handler, NULL);

  power_set_out_enabled(false);

  return true;
}
