#include "soyosource.h"

#include "mgos.h"
#include "mgos_uart.h"
#include "mgos_timers.h"
#include "mgos_prometheus_metrics.h"
#include "mgos_crontab.h"

static uint8_t soyo_out[8] = { 0x24, 0x56, 0x00, 0x21, 0x00, 0x00, 0x80, 0x08 };
static bool soyo_enabled = false;
const static uint8_t soyo_header[4] = { 0x23, 0x01, 0x01, 0x00 };

static uint8_t soyo_operation_mode = 0;
static float soyo_voltage = -1.0f;
static float soyo_current = -1.0f;
static uint16_t soyo_ac_voltage = 0;
static float soyo_ac_frequency = -1.0f;
static float soyo_temperature = -1.0f;
static void soyosource_metrics(struct mg_connection *nc, void *data) {  
  mgos_prometheus_metrics_printf(
      nc, GAUGE, "soyo_current", "|DC Current out (Ampere)",
      "{type=\"soyo\", unit=\"0\"} %f", soyo_current);

  mgos_prometheus_metrics_printf(
      nc, GAUGE, "soyo_voltage", "DC Voltage (Volt)",
      "{type=\"soyo\", unit=\"0\"} %f", soyo_voltage);

  mgos_prometheus_metrics_printf(
      nc, GAUGE, "soyo_operation_mode", "Operation mode",
      "{type=\"soyo\", unit=\"0\"} %d", soyo_operation_mode);

  mgos_prometheus_metrics_printf(
      nc, GAUGE, "soyo_ac_voltage", "AC Voltage (Volt)",
      "{type=\"soyo\", unit=\"0\"} %d", soyo_ac_voltage);
    
  mgos_prometheus_metrics_printf(
    nc, GAUGE, "soyo_ac_frequency", "AC Frequency (Hz)",
    "{type=\"soyo\", unit=\"0\"} %f", soyo_ac_frequency);

  mgos_prometheus_metrics_printf(
    nc, GAUGE, "soyo_temperature", "Temperature (Celcius)",
    "{type=\"soyo\", unit=\"0\"} %f", soyo_temperature);

  (void) data;
}

static void soyosource_dispatcher_cb(int uart, void *arg) {
  static struct mbuf lb = {0};
  if(uart != mgos_sys_config_get_soyosource_uart()) {
    return;
  }
  size_t rx_av = mgos_uart_read_avail(uart);
  if(rx_av == 0) {
    return;
  }
  mgos_uart_read_mbuf(uart, &lb, rx_av);
  if(lb.len < 14) {
    return;
  }
  for(int i=0; i<4; i++) {
    if(soyo_header[i] != lb.buf[i]) {
      mbuf_remove(&lb, i+1);
      return;
    }
  }

  uint8_t *data = (uint8_t *) (lb.buf + 4);
  uint8_t operation_mode = data[0];
  float voltage = 0.1f * ((data[1] << 8) | (data[2] << 0));
  float current = 0.1f * ((data[3] << 8) | (data[4] << 0));
  uint16_t ac_voltage = ((data[5] << 8) | (data[6] << 0));
  float ac_frequency = data[7] * 0.5f;
  float temperature = (((data[8] << 8) | (data[9] << 0)) - 300) * 0.1f;
  
  // uart response is noisy, only take useful values
  soyo_operation_mode = operation_mode;
  if( (soyo_voltage == -1 && voltage < 65)
    || abs(soyo_voltage - voltage) < 5) {
    soyo_voltage = voltage;
  }
  if( (soyo_current == -1 && current < 25)
    || abs(soyo_current - current) < 25) {
    soyo_current = current;
  }
  if( (soyo_ac_voltage == 0 && ac_voltage < 300)
    || abs(soyo_ac_voltage - ac_voltage) < 50) {
    soyo_ac_voltage = ac_voltage;
  }
  if( (soyo_ac_frequency == -1 && ac_frequency < 100)
    || abs(soyo_ac_frequency - ac_frequency) < 10) {
    soyo_ac_frequency = ac_frequency;
  }
  if( (soyo_temperature == -1 && temperature < 100)
    || abs(soyo_temperature - temperature) < 10) {
    soyo_temperature = temperature;
  }

  mbuf_remove(&lb, 14);
  LOG(LL_INFO, ("Battery: %d : %.1fV, %.1fA, ~%uV, %.1fHz, %.1fC", 
   soyo_operation_mode,  soyo_voltage, soyo_current, soyo_ac_voltage, soyo_ac_frequency, soyo_temperature));
}

static bool soyosource_send(const uint8_t *packet) {
  if(!soyosource_get_enabled()) {
    LOG(LL_WARN, ("soyosoure is disabled: cannot send data."));
    return false;
  }
  int uart = mgos_sys_config_get_soyosource_uart();
  bool result = (mgos_uart_write(uart, packet, 8) == 8);
  mgos_uart_flush(uart);
  //mgos_uart_schedule_dispatcher(uart, false);
  //LOG(LL_INFO, ("sent a packet, available for read [on: %d] %d", mgos_uart_is_rx_enabled(uart), mgos_uart_read_avail(uart)));
  return result;
}

static void soyosource_feed_cb(void *arg) {
  if(soyosource_get_enabled()) {
    uint8_t* out = (uint8_t*) arg;
    soyosource_send(out);
  } 
}

static void soyosource_feed_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  soyosource_feed_cb(userdata);
  (void) payload;
}

static void soyosource_status_cb(void *arg) {
  if(soyosource_get_enabled()) {
    soyosource_request_status();
  }
  (void) arg;
}

static void soyosource_status_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  soyosource_status_cb(userdata);
  (void) payload;
}

void soyosource_init() {
  int uart = mgos_sys_config_get_soyosource_uart();
  if(uart == -1) {
    LOG(LL_INFO, ("UART disabled for soyosource"));
    soyo_enabled = false;
    return;
  }
  struct mgos_uart_config ucfg;
  mgos_uart_config_set_defaults(uart, &ucfg);

  ucfg.baud_rate = 4800;
  ucfg.parity = MGOS_UART_PARITY_NONE;
  ucfg.stop_bits = MGOS_UART_STOP_BITS_1;
 // ucfg.rx_buf_size = 31;
 // ucfg.tx_buf_size = 31;
 #if CS_PLATFORM == CS_P_ESP32
  ucfg.dev.hd = true; // n/a esp8266
 #endif
  // ucfg.dev.rx_fifo_full_thresh = 16;

  if (!mgos_uart_configure(uart, &ucfg)) {
    LOG(LL_ERROR, ("Failed to configure UART%d", uart));
    soyo_enabled = false;
    return;
  }
  soyo_enabled = true;
  soyosource_request_status();
  mgos_uart_set_dispatcher(uart, soyosource_dispatcher_cb, NULL);
  mgos_uart_set_rx_enabled(uart, true);

  // int feed_interval = mgos_sys_config_get_soyosource_feed_interval();
  // int status_interval = mgos_sys_config_get_soyosource_status_interval();
  // mgos_set_timer(feed_interval, MGOS_TIMER_REPEAT, soyosource_feed_cb, soyo_out);
  // mgos_set_timer(status_interval, MGOS_TIMER_REPEAT, soyosource_status_cb, NULL);

  mgos_crontab_register_handler(mg_mk_str("soyosource.feed"), soyosource_feed_crontab_handler, soyo_out);
  mgos_crontab_register_handler(mg_mk_str("soyosource.status"), soyosource_status_crontab_handler, NULL);

  mgos_prometheus_metrics_add_handler(soyosource_metrics, NULL);
  LOG(LL_INFO, ("uart %d enabled", uart));
}


bool soyosource_get_enabled() {
  return soyo_enabled && mgos_sys_config_get_soyosource_uart() != -1;
}

void soyosource_set_enabled(bool enabled) {
  if(!enabled) {
    soyosource_set_power_out(0);
  }
  soyo_enabled = enabled;
}


void soyosource_set_power_out(int power) {
  int p = power * (1.0 + mgos_sys_config_get_soyosource_loss());
  soyo_out[4] = p >> 8;
  soyo_out[5] = p & 0xFF;
  soyo_out[7] = (264 - soyo_out[4] - soyo_out[5]) & 0xFF;

  soyosource_send(soyo_out);
}

int soyosource_get_power_out() {
  return soyo_voltage * soyo_current * (1.0 - mgos_sys_config_get_soyosource_loss());
}


void soyosource_request_status() {
static const uint8_t status_request[8] = { 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  if(!soyosource_send(status_request)) {
    LOG(LL_WARN, ("Failed to request soyosource status"));
  }
  //LOG(LL_INFO, ("request soyosource status"));
}

float soyosource_get_last_voltage() {
  return soyo_voltage;
}

float soyosource_get_last_current() {
  return soyo_current;
}