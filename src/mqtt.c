#include "mqtt.h"
#include "power.h"

#include "mgos.h"
#include "mgos_mqtt.h"

static void topic_total_power_handler(struct mg_connection *nc, const char *topic,
                              int topic_len, const char *msg, int msg_len,
                              void *ud) {
  if(msg_len == 0) {
    LOG(LL_INFO, ("empty mqtt message for topic %s", topic));
    return;
  }

  float power = strtof(msg, NULL);
  power_set_total_power(power);

  (void) topic_len;
  (void) ud;
}

bool mqtt_init() {

  const char* total_power_topic = mgos_sys_config_get_power_total_power_topic();
  if(total_power_topic == NULL || strlen(total_power_topic) > 0) {
    LOG(LL_INFO, ("no topic configured to receive total power values"));
    return false;
  } 
  mgos_mqtt_sub(total_power_topic, topic_total_power_handler, NULL);

  return true;
}