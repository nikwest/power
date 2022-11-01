#include "fan.h"

#include "mgos.h"
#include "mgos_pwm.h"
#include "mgos_prometheus_metrics.h"

#define FAN_NUM 1
#define FAN_PWM_FREQ 25000
#define TIMER_INTERVAL 1000

static struct fan {
  const struct mgos_config_fan *config;
  int index;
  int rpm;
  float pwm;
  int count; // ISR context
} fan[FAN_NUM];

static void fan_metrics(struct mg_connection *nc, void *data) {
  for(int i = 0; i < FAN_NUM; i++) {
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "fan_rpm", "Fan rpm",
        "{unit=\"%d\"} %d", i, fan[i].rpm);
    mgos_prometheus_metrics_printf(
      nc, GAUGE, "fan_pwm", "Fan pwm",
      "{unit=\"%d\"} %f", i, fan[i].pwm);
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "fan_count", "Fan count",
        "{unit=\"%d\"} %d", i, fan[i].count);
 }
}

static const struct mgos_config_fan * fan_get_config(int fan) {
  switch (fan) {
  case 0:
    return mgos_sys_config_get_fan();
  // case 1:
  //   return mgos_sys_config_get_fan1();
  // case 2:
  //   return mgos_sys_config_get_fan2();
  default:
    return NULL;
  }
}

static void rpm_handler(void *arg) {
  for(int i = 0; i < FAN_NUM; i++) {
//    mgos_gpio_disable_int(fan[i].config->rpm_pin);
    fan[i].rpm = fan[i].count * 30000 / TIMER_INTERVAL;
    fan[i].count = 0;
//    mgos_gpio_enable_int(fan[i].config->rpm_pin);
  }
}

IRAM void counter_isr(int pin, void *arg) {
  struct fan *fan = (struct fan *) arg;
  fan->count++;
  mgos_gpio_clear_int(pin);
}

bool fan_init() {
  mgos_prometheus_metrics_add_handler(fan_metrics, NULL);
  for(int i = 0; i < FAN_NUM; i++) {
    fan[i].index = i;
    fan[i].pwm = 0.0;
    fan[i].count = 0;
    fan[i].config = fan_get_config(i);
    if(fan[i].config->enable) {
      mgos_gpio_set_mode(fan[i].config->pwm_pin, MGOS_GPIO_MODE_OUTPUT_OD);
      if(!mgos_pwm_set(fan[i].config->pwm_pin, FAN_PWM_FREQ, fan[i].pwm)) {
        LOG(LL_ERROR, ("Failed to set pwm for fan %d [%d]", i, fan[i].config->pwm_pin));
      }
      mgos_gpio_set_mode(fan[i].config->rpm_pin, MGOS_GPIO_MODE_INPUT);
      mgos_gpio_setup_input(fan[i].config->rpm_pin, MGOS_GPIO_PULL_UP);
      if(!mgos_gpio_set_int_handler_isr(fan[i].config->rpm_pin, MGOS_GPIO_INT_EDGE_NEG,
                                   counter_isr, (void *) &(fan[i]))) {
        LOG(LL_ERROR, ("Failed to set rpm interrupt for fan %d [%d]", i, fan[i].config->rpm_pin));
      }
      mgos_gpio_enable_int(fan[i].config->rpm_pin);
    }
  }
  mgos_set_timer(TIMER_INTERVAL, MGOS_TIMER_REPEAT, rpm_handler, NULL);

  return true;
}

void fan_set_speeds(int s) {
  for(int i = 0; i < FAN_NUM; i++) {
    fan_set_speed(i, s);
  }
}

void fan_set_speed(int i, int s) {
  if(i >= FAN_NUM || i < 0) {
    LOG(LL_ERROR, ("Invalid fan %d", i));
    return;
  }
  if(!fan[i].config->enable) {
    LOG(LL_WARN, ("fan %d is disabled.", i));
    return;
  }

  float pwm = s / 100.0;
  if(mgos_pwm_set(fan[i].config->pwm_pin, FAN_PWM_FREQ, pwm)) {
    fan[i].pwm = pwm;
  } else {
    LOG(LL_ERROR, ("Failed to set pwm for fan %d [%d] to %d%%", i, fan[i].config->pwm_pin, s));
  } 
}