#include "ds18xxx.h"

#include "mgos.h"
#include "mgos_onewire.h"
#include "mgos_prometheus_metrics.h"
#include "mgos_timers.h"
#include "mgos_crontab.h"


/* Model IDs */
#define DS18S20MODEL 0x10
#define DS18B20MODEL 0x28
#define DS1822MODEL 0x22
#define DS1825MODEL 0x3B
#define DS28EA00MODEL 0x42

#define CONVERT_T 0x44
#define READ_SCRATCHPAD 0xBE
#define CONVERSION_TIME 750


struct ds18xxx_rom {
  uint64_t family : 8;
  uint64_t serial : 48;
  uint64_t crc : 8;
};
struct __attribute__ ((__packed__)) ds18xxx_scratchpad {
  int16_t temperature;
  uint8_t th;
  uint8_t tl;
  union {
    uint8_t rsvd_1 : 5;
    uint8_t resolution : 2;
    uint8_t rsvd_0 : 1;
    uint8_t val;
  } cfg;
  uint8_t rfu;
  uint8_t count_remain;
  uint8_t count_per_c;
  uint8_t crc;
};

static struct ds18xxx_rom rom;
static struct mgos_onewire *onewire = NULL;
static int conversion_time = CONVERSION_TIME;
static float current_temperature = 0.0;

static void ds18xxx_metrics(struct mg_connection *nc, void *data) {
  mgos_prometheus_metrics_printf(
        nc, GAUGE, "ds18xxx_temperature", "Current temperature in Celcius",
        "%f", current_temperature);
  (void) data;
}

static void ds18xxx_temperature_cb(void *userdata) {
  static struct ds18xxx_scratchpad result;
  struct mgos_onewire *ow = (struct mgos_onewire*) userdata;
  if (!mgos_onewire_reset(ow)) {
    LOG(LL_ERROR, ("ds18xxx: Bus reset failed"));
    return;
  }
  mgos_onewire_select(ow, (uint8_t *) &rom);
  mgos_onewire_write(ow, READ_SCRATCHPAD);
  mgos_onewire_read_bytes(ow, (uint8_t *) &result, sizeof(result));
  uint8_t crc = mgos_onewire_crc8((uint8_t *) &result, sizeof(result) - 1);
  if (crc != result.crc) {
    LOG(LL_ERROR, ("ds18xxx: Invalid scratchpad CRC: %#02x vs %#02x", crc, result.crc));
    return;
  }

  if (rom.family == DS18S20MODEL) {
    current_temperature = ((int16_t)(result.temperature & 0xFFFE) / 2.0) - 0.25 +
              ((float) (result.count_per_c - result.count_remain) / result.count_per_c);
  } else {
    current_temperature = result.temperature * 0.0625f;
  }

  switch (result.cfg.resolution) {
    case 0:
      conversion_time = 94;
    case 1:
      conversion_time = 188;
    case 2:
      conversion_time = 375;
    default:
      conversion_time = 750;
  }

}

static void ds18xxx_update_cb(void *userdata) {
  struct mgos_onewire *ow = (struct mgos_onewire*) userdata;
  if (!mgos_onewire_reset(ow)) {
    LOG(LL_ERROR, ("ds18xxx: Bus reset failed"));
    return;
  }
  mgos_onewire_select(ow, (uint8_t *) &rom);
  mgos_onewire_write(ow, CONVERT_T);
  mgos_set_timer(conversion_time, 0, ds18xxx_temperature_cb, userdata);
}

static void ds18xxx_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  LOG(LL_DEBUG, ("%.*s crontab job fired!", action.len, action.p));
  ds18xxx_update_cb(userdata);
}

bool ds18xxx_init() {
  int pin = mgos_sys_config_get_onewire_pin();
  if(pin == -1) {
    LOG(LL_INFO, ("One wire disabled."));
    return false;
  } 
  LOG(LL_INFO, ("One wire pin %d.", pin));
  onewire = mgos_onewire_create(pin);
  if(mgos_onewire_next(onewire, (uint8_t *) &rom, 0)) {
    uint8_t family = rom.family;
    LOG(LL_INFO, ("Found device (family %u)", family));
  } else {
    LOG(LL_ERROR, ("Couldn't find onewire device"));
    return false;
  }
  mgos_crontab_register_handler(mg_mk_str("ds18xxx.temperature"), ds18xxx_crontab_handler, onewire);
  mgos_prometheus_metrics_add_handler(ds18xxx_metrics, NULL);
  return true;
}


float ds18xxx_get_temperature() {
  return current_temperature;
}