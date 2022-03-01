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
static float soyo_voltage = 0.0f;
static float soyo_current = 0.0f;
static uint16_t soyo_ac_voltage = 0;
static float soyo_ac_frequency = 0.0f;
static float soyo_temperature = 0.0f;
static bool uart_lock = false; // poor man's half duplex
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
  if(uart != mgos_sys_config_get_soyosource_uart()) {
    return;
  }
  static int i = 0;
  uint8_t c;
  while(mgos_uart_read(uart, &c, 1) == 1) {
    if(c == soyo_header[i]) { 
      i++; 
    } else {
      i = 0;
    }
    if(i == 4) { break; }
  }
  if(mgos_uart_read_avail(uart) < 10) {
    return;
  }
  uint8_t data[10];
  if(mgos_uart_read(uart, data, 10) < 10) {
    LOG(LL_ERROR, ("Couldn't read 10 bytes from uart"));
    return;
  }

  uart_lock = false;

  // static char hex[255]; 
  // mg_hexdump(data, 10, hex, 255);
  // LOG(LL_INFO, ("%s", hex)); // Output "0000  01 02 03 00";

  soyo_operation_mode = data[0];
  soyo_voltage = 0.1f * ((data[1] << 8) | (data[2] << 0));
  soyo_current = 0.1f * ((data[3] << 8) | (data[4] << 0));
  soyo_ac_voltage = ((data[5] << 8) | (data[6] << 0));
  soyo_ac_frequency = data[7] * 0.5f;
  soyo_temperature = (((data[8] << 8) | (data[9] << 0)) - 300) * 0.1f;

  i = 0;
  LOG(LL_INFO, ("Status received: %d: %.1fV, %.1fA, ~%uV, %.1fHz, %.1fC", 
   soyo_operation_mode,  soyo_voltage, soyo_current, soyo_ac_voltage, soyo_ac_frequency, soyo_temperature));
}

static bool soyosource_send(uint8_t *packet) {
  int uart = mgos_sys_config_get_soyosource_uart();
  bool result = (mgos_uart_write(uart, packet, 8) == 8);
  mgos_uart_flush(uart);
  return result;
}

static void soyosource_feed_cb(void *arg) {
  if(soyosource_get_enabled() && !uart_lock) {
    uint8_t* out = (uint8_t*) arg;
    soyosource_send(out);
  } 
}

static void soyosource_feed_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  soyosource_feed_cb(userdata);
  (void) payload;
  (void) userdata;
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
  (void) userdata;
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
  ucfg.rx_buf_size = 16;
  ucfg.tx_buf_size = 16;
  // ucfg.dev.hd = true; // n/a esp8266

  if (!mgos_uart_configure(uart, &ucfg)) {
    LOG(LL_ERROR, ("Failed to configure UART%d", uart));
  }
  soyo_enabled = true;
  soyosource_request_status();
  mgos_uart_set_dispatcher(uart, soyosource_dispatcher_cb, NULL);

  // int feed_interval = mgos_sys_config_get_soyosource_feed_interval();
  // int status_interval = mgos_sys_config_get_soyosource_status_interval();
  // mgos_set_timer(feed_interval, MGOS_TIMER_REPEAT, soyosource_feed_cb, soyo_out);
  // mgos_set_timer(status_interval, MGOS_TIMER_REPEAT, soyosource_status_cb, NULL);

  mgos_crontab_register_handler(mg_mk_str("soyosource.feed"), soyosource_feed_crontab_handler, soyo_out);
  mgos_crontab_register_handler(mg_mk_str("soyosource.status"), soyosource_status_crontab_handler, NULL);

  mgos_prometheus_metrics_add_handler(soyosource_metrics, NULL);
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

  if(!uart_lock) {
    soyosource_send(soyo_out);
  } else {
    LOG(LL_INFO, ("UART locked ... sending delayed"));
  }
}

int soyosource_get_power_out() {
  return soyo_voltage * soyo_current * (1.0 - mgos_sys_config_get_soyosource_loss());
}


void soyosource_request_status() {
  uint8_t status_request[8] = { 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  uart_lock = true;
  if(!soyosource_send(status_request)) {
    LOG(LL_WARN, ("Failed to request soyosource status"));
  }
}