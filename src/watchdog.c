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
#include "ds18xxx.h"
#include "fan.h"


#include <math.h>


#define PRICE_INVALID -1.0

#define MIN_TEMP 25.0f
#define MAX_TEMP 31.0f
#define MIN_FAN_SPEED 10

static const float monthly_radiation[] = { 30, 45, 80, 125, 160, 165, 165, 140, 95, 60, 30, 25 };
static const float performance_ratio = 0.75;
static int estimated_yield = 0;
static float price_avg = 0;
static float price_sigma = 0;
static float price_current = 0;
static float price_limit = DEFAULT_PRICE_LIMIT;

static int estimate_yield(darksky_day_forecast_t forecast) {
  int peak = mgos_sys_config_get_solar_peak_power();
  int base = mgos_sys_config_get_power_out_on();
  const struct tm *t = localtime(&forecast.time);
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

    (void) data;
}

static void handle_temperature() {
  float temp = ds18xxx_get_temperature();
  if(temp == 0) {
    fan_set_speeds(100);
    LOG(LL_WARN, ("Temperature is 0. Sensor failed?"));
    return;
  }
  if(temp < MIN_TEMP-1) {
    fan_set_speeds(0); // turn off later to avoid oscilating fans
  } else if(temp > MAX_TEMP) {
    fan_set_speeds(100);
  } else if(temp > MIN_TEMP)  {
    int speed = (int) ((temp - MIN_TEMP) / (MAX_TEMP - MIN_TEMP) * 100.0);
    fan_set_speeds(MAX(MIN_FAN_SPEED, speed));
  }
}

static void watchdog_handler(void *data) {
  int num_cells = mgos_sys_config_get_battery_num_cells();
  float battery_max = mgos_sys_config_get_battery_cell_voltage_max() * num_cells;
  float battery_min = mgos_sys_config_get_battery_cell_voltage_min() * num_cells;
  float battery = battery_read_voltage();
  power_state_t state = power_get_state();
  //battery_state_t battery_state = battery_get_state();

  switch (state)
  {
  case power_in:
    if(battery > battery_max) {
      LOG(LL_INFO, ("battery full: %.2f battery_max: %.2f", battery, battery_max));
      power_set_state(power_off);
      battery_set_state(battery_full);
    }
    break;
  case power_out:
    if(battery < battery_min) {
      LOG(LL_INFO, ("battery empty: %.2f battery_min: %.2f", battery, battery_min));
      power_set_state(power_off);
      battery_set_state(battery_empty);
    } else if(!power_get_out_enabled()) {
      power_set_state(power_off);
    } 
    break;
  default:
    break;
  }
  state = power_get_state();

  handle_temperature();

  LOG(LL_INFO, ("power_state: %d battery_voltage: %.2fV temp: %.1fC", state, battery, ds18xxx_get_temperature()));

  // mgos_dash_notifyf(
  //   "Status", 
  //   "{power_state: %d, battery_state: %d, battery_voltage: %f}", 
  //   state, battery_state, battery
  // );

  (void) data;
}

static void watchdog_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  LOG(LL_DEBUG, ("%.*s crontab job fired!", action.len, action.p));
  watchdog_handler(userdata);

  (void) payload;
}

static void power_out_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  watchdog_evaluate_power_out(price_limit, NULL);
  (void) payload;
  (void) userdata;
}

static void discovergy_handler(double update, float power, void* cb_arg) {
  // char time[20];
  // mgos_strftime(time, 32, "%x %X", update);
  // LOG(LL_INFO, ("%s: %.2f", time, power));
  
  float lag = mg_time() - update;
  float max_lag = mgos_sys_config_get_power_max_lag();
  if(max_lag > 0 && lag > max_lag) {
    LOG(LL_WARN, ("Discovergy data outdated: %f", lag));
    power_set_state(power_off);
    return;
  }

  power_set_total_power(power);

  (void) cb_arg;
}

static void darksky_handler(darksky_day_forecast_t *entries, int length, void *cb_arg) {
  if(length == 0) {
    LOG(LL_INFO, ("no weather data available"));
    return;
  }
  estimated_yield = estimate_yield(entries[0]);

  (void) cb_arg;
}

static void awattar_handler(awattar_pricing_t *entries, int length, void *cb_arg) {
  price_avg = 0.0;
  price_sigma = 0.0;
  time_t now = time(NULL);                 

  if(entries == NULL || length == 0) {
    price_sigma = PRICE_INVALID; // invalid
  }
  for(int i = 0; i<length; i++) {
    price_avg += entries[i].price;
    if(entries[i].start <= now && now < entries[i].end) {
      price_current = entries[i].price;
    }
  }
  price_avg /= length;
  for(int i = 0; i<length; i++) {
    price_sigma += (entries[i].price - price_avg) * (entries[i].price - price_avg);
  }
  price_sigma /= length;
  price_sigma = sqrtf(price_sigma);

  (void) cb_arg;
}


bool watchdog_init() {
  mgos_prometheus_metrics_add_handler(watchdog_metrics, NULL);
  discovery_set_update_callback(discovergy_handler, NULL);
  //darksky_set_update_callback(darksky_handler, NULL);
  awattar_set_update_callback(awattar_handler, NULL);
  mgos_crontab_register_handler(mg_mk_str("watchdog"), watchdog_crontab_handler, NULL);
  mgos_crontab_register_handler(mg_mk_str("power_out"), power_out_crontab_handler, NULL);

  power_set_out_enabled(true);

  return true;
}

bool watchdog_evaluate_power_out(float limit, float *price) {
  price_limit = limit;
  price_current = 0.0;      
  int battery_soc = battery_get_soc();  
  float battery_factor = 1.0 - (battery_soc / 100.0);              
  if(price_sigma == PRICE_INVALID) {
    LOG(LL_WARN, ("invalid price, power out %d", power_get_out_enabled()));
    return power_get_out_enabled();
  }
  time_t now = time(NULL);                 
  awattar_pricing_t* current = awattar_get_entry(now);
  if(current == NULL) {
    LOG(LL_WARN, ("no current price, power out %d", power_get_out_enabled()));
    return power_get_out_enabled();
  }
  price_current = current->price;
  bool enabled = (price_limit == DEFAULT_PRICE_LIMIT) 
    ? (price_current > (price_avg + price_sigma * battery_factor))
    : (price_current > price_limit);

  power_set_out_enabled(enabled);
  if(price != NULL) {
    *price = price_current;
  }
  return power_get_out_enabled();
}


enum { 
    measure_start, 
    measure_running, 
    measure_done 
} watchdog_measure_state;
int call_count = 0;
float start_power = 0;

static void measure_discovergy_handler(double update, float power, void* cb_arg) {
  int p = (int) cb_arg;
  switch (watchdog_measure_state) {
  case measure_start:
    call_count = 0;
    start_power = power;
    power_set_total_power(power + p);
    watchdog_measure_state = measure_running;
    break;
  case measure_running:
    if(abs(power) <  abs(start_power + p/2)) {
      call_count++;
      power_set_total_power(power + p);
    } else {
      start_power = 0;
      watchdog_measure_state = measure_done;
      power_set_total_power(power);
    }
    break;
  case measure_done:
    LOG(LL_INFO, ("Measured call count: %d for power change %d", call_count, p));
    call_count = 0;
    discovery_set_update_callback(discovergy_handler, NULL);
  default:
    break;
  }
}

void watchdog_measure_lag(int power) {
  watchdog_measure_state = measure_start;
  discovery_set_update_callback(measure_discovergy_handler, power);
}