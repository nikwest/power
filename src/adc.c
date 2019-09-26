#include "adc.h"

#include "mgos_ads1x1x.h"
#include "mgos_prometheus_metrics.h"

static struct mgos_ads1x1x *ads1115 = NULL;

static void adc_cb(void *data) {
  struct mgos_ads1x1x *d = (struct mgos_ads1x1x *)data;
  int16_t res[4];

  if (!d) {
    LOG(LL_ERROR, ("ADC device not available"));
    return;
  }
  for(int i=0; i<4; i++) {
    if (!mgos_ads1x1x_read(d, i, &res[i])) {
      LOG(LL_ERROR, ("Could not read device"));
      return;
    }
  }
  LOG(LL_INFO, ("chan={%6d, %6d, %6d, %6d}", res[0], res[1], res[2], res[3]));
}

static void adc_metrics(struct mg_connection *nc, void *data) {
    struct mgos_ads1x1x *d = (struct mgos_ads1x1x *)data;
    
    float result = adc_read_power_in_current();
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "power_in_current", "Current (Amperes)",
        "{type=\"ads1115\", unit=\"0\", chan=\"0\"} %f", result);

    result = adc_read_power_out_current();
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "power_out_current", "Current (Amperes)",
        "{type=\"ads1115\", unit=\"0\", chan=\"0\"} %f", result);

    result = adc_read_battery_voltage();
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "battery_voltage", "Voltage (Volts)",
        "{type=\"ads1115\", unit=\"0\", chan=\"0\"} %f", result);

  (void) data;
}

static int adc_read_channels(int channel) {
    int16_t raw;

    if (!ads1115) {
        LOG(LL_ERROR, ("ADC device not available"));
        return 0;
    }
    if (!mgos_ads1x1x_read_diff(ads1115, channel, channel+1, &raw)) {
        LOG(LL_ERROR, ("Could not read device"));
        return 0;
    }
    return raw;
}

static int adc_read_channel(int channel) {
    int16_t raw;

    if (!ads1115) {
        LOG(LL_ERROR, ("ADC device not available"));
        return 0;
    }
    if (!mgos_ads1x1x_read(ads1115, channel, &raw)) {
        LOG(LL_ERROR, ("Could not read device"));
        return 0;
    }
    return raw;
}

bool adc_init() {

  if (!(ads1115 = mgos_ads1x1x_create(mgos_i2c_get_global(), 0x48, ADC_ADS1115))) {
    LOG(LL_ERROR, ("Could not create ADS1115"));
    return false;
  }
  LOG(LL_INFO, ("Setup ADS1115"));
  mgos_ads1x1x_set_fsr(ads1115, MGOS_ADS1X1X_FSR_2048);
  mgos_ads1x1x_set_dr(ads1115, MGOS_ADS1X1X_SPS_MIN);

  //mgos_set_timer(10000 /* ms */, MGOS_TIMER_REPEAT, adc_cb, ads1115);

  mgos_prometheus_metrics_add_handler(adc_metrics, ads1115);

  return true;
}

float adc_read_battery_voltage() {
    int channel = mgos_sys_config_get_power_battery_voltage_channel();
    int factor = mgos_sys_config_get_power_battery_voltage_factor();
    return (float) adc_read_channels(channel) / (float) factor;
}

float adc_read_power_in_current() {
    int channel = mgos_sys_config_get_power_in_current_channel();
    int factor = mgos_sys_config_get_power_in_current_factor();
    return (float) adc_read_channels(channel) / (float) factor;
}

float adc_read_power_out_current() {
    int channel = mgos_sys_config_get_power_out_current_channel();
    int factor = mgos_sys_config_get_power_out_current_factor();
    return 0;  //(float) adc_read_channel(channel) / (float) factor;
}
