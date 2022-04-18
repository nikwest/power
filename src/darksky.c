#include "darksky.h"

#include "mgos.h"
#include "mgos_location.h"
#include "mgos_crontab.h"

#define DAY_ARRAY_SIZE 1

static const char *urlf = "https://api.darksky.net/forecast/%s/%.4f,%.4f?exclude=currently,hourly,alerts,flags&units=si";
static char *url = NULL;

static darksky_update_callback callback = NULL;
static void *callback_arg;

static darksky_day_forecast_t entries[DAY_ARRAY_SIZE];
static int entries_count = 0;

static void darksky_request_handler(void *data);
static void darksky_response_handler(struct mg_connection *nc, int ev, void *ev_data, void *ud);


static void scan_array(const char *str, int len, void *user_data) {
    struct json_token t;
    int i;
    float clouds;
    int64_t time, sunrise, sunset;
//    char time[32];

    for (i = 0; i<DAY_ARRAY_SIZE && json_scanf_array_elem(str, len, "", i, &t) > 0; i++) {
      json_scanf(t.ptr, t.len, "{time: %lld, sunriseTime: %lld, sunsetTime: %lld, cloudCover: %f", &time, &sunrise, &sunset, &clouds);
      entries[i].time = time;
      entries[i].sunrise = sunrise;
      entries[i].sunset = sunset;
      entries[i].clouds = clouds;
      LOG(LL_INFO,("%lld: %lldm %f", time, (sunset - sunrise)/60, clouds));
    }
    entries_count = i;

    (void) user_data;
}

static void got_ip_handler(int ev, void *evd, void *data) {
  if (ev != MGOS_NET_EV_IP_ACQUIRED) {
    return;
  }
  darksky_request_handler(data);

  (void) evd;
}

static void darksky_response_handler(struct mg_connection *nc, int ev, void *ev_data, void *ud) {
  struct http_message *hm = (struct http_message *) ev_data;
  switch (ev) {
    case MG_EV_CONNECT:
      if (*(int *) ev_data != 0) {
        LOG(LL_ERROR, ("connect() failed[%d]: %s\n", (*(int *) ev_data), url));
      }
      break;
    case MG_EV_HTTP_REPLY:
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      //LOG(LL_INFO,("Response: %.*s", hm->body.len, hm->body.p));
      if (1 != json_scanf(hm->body.p, hm->body.len, "{ daily: {data: %M } }", &scan_array)) {
        LOG(LL_ERROR, ("failed to parse json response\n"));
        break;
      } 
      LOG(LL_INFO, ("Successfully parsed weather data\n"));
      if(callback != NULL) {
        callback(entries, entries_count, callback_arg);
      }
      break;
    case MG_EV_CLOSE:
      LOG(LL_DEBUG, ("Server closed connection"));
      break;
    default:
      break;
  }

  (void) ud;
}

static void darksky_request_handler(void *data) {
  LOG(LL_INFO, ("Server send request\n"));
  mg_connect_http(mgos_get_mgr(), darksky_response_handler, data, url, NULL, NULL);
}

static void darksky_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  LOG(LL_DEBUG, ("%.*s crontab job fired!", action.len, action.p));
  darksky_request_handler(NULL);
}

bool darksky_init() {
  const struct mgos_config_darksky *config = mgos_sys_config_get_darksky();
  if(config == NULL) {
    LOG(LL_ERROR, ("Darksky config missing in mos.yml"));
    return false;
  }
  struct mgos_location_lat_lon location;
  if(!mgos_location_get(&location)) {
    LOG(LL_ERROR, ("Device location config missing in mos.yml"));
    return false;
  }

  int len = strlen(urlf)+strlen(config->key)+14;
  url = malloc((len+1)*sizeof(char));
  int n = snprintf(url, len, urlf, config->key, location.lat, location.lon);
  if(n < 0 || n >= len) {
    LOG(LL_ERROR, ("Cannot create Darksky url"));
    return false;
  }

  LOG(LL_INFO, ("url %s", url));

  mgos_crontab_register_handler(mg_mk_str("darksky"), darksky_crontab_handler, NULL);

  //mgos_set_timer(30000 /* ms */, 0, darksky_request_handler, NULL);
  mgos_event_add_handler(MGOS_NET_EV_IP_ACQUIRED, got_ip_handler, NULL);

  return true;
}

void darksky_set_update_callback(darksky_update_callback cb, void *cb_arg) {
  callback = cb;
  callback_arg = cb_arg;
}

int darksky_get_day_forecast_count() {
  return entries_count;
}

darksky_day_forecast_t* darksky_get_day_forecast() {
  return entries;
}
