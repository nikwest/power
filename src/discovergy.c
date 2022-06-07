
#include "discovergy.h"

#include "mgos.h"
#include "mgos_mongoose.h"
#include "mgos_prometheus_metrics.h"
#include "mgos_crontab.h"

static discovergy_update_callback callback = NULL;
static void *callback_arg;

static const char *urlf = "https://api.discovergy.com/public/v1/last_reading?fields=power&meterId=%s";

static char *url = NULL;
static char *auth;

static int last_power = 0;
static int64_t last_update = 0;
static double last_lag = 0;
static double last_response_time = 0;
static double last_request_start = 0;
static int connection_count = 0;
static int connection_failed_count = 0;

static struct mbuf request;
static struct mg_conn *connection = NULL;

static void discovergy_metrics(struct mg_connection *nc, void *data) {
  mgos_prometheus_metrics_printf(
        nc, GAUGE, "discovergy_total_power", "Current total power in mW",
        "%d", last_power);
  mgos_prometheus_metrics_printf(
        nc, GAUGE, "discovergy_lag", "Receive lag of data in seconds",
        "%f", last_lag);
  mgos_prometheus_metrics_printf(
        nc, GAUGE, "discovergy_response_time", "Response time in seconds",
        "%f", last_response_time);
  mgos_prometheus_metrics_printf(
        nc, GAUGE, "discovergy_connections_count", "Current count of opened connections",
        "%d", connection_count);
  mgos_prometheus_metrics_printf(
        nc, GAUGE, "discovergy_connections_failed_count", "Current count of failed connections",
        "%d", connection_failed_count);

  (void) data;
}

static void discovergy_response_handler(struct mg_connection *nc, int ev, void *ev_data, void *ud) {
  struct http_message *hm = (struct http_message *) ev_data;
  switch (ev) {
    case MG_EV_CONNECT:
      LOG(LL_INFO, ("Server connection"));
      if (* (int *) ev_data != 0) {
        connection_failed_count++;
        nc->flags |= MG_F_CLOSE_IMMEDIATELY;
        LOG(LL_ERROR, ("connect() failed[%d]: %s\n", * (int *) ev_data, url));
        break;
      }
      //mg_set_timer(nc, 0);  // Clear connect timer
      connection_count++;
      if(request.len == 0) {
        mbuf_append(&request, nc->send_mbuf.buf, nc->send_mbuf.len);
        LOG(LL_INFO, ("Stored request (%db):/n%.*s", (int) nc->send_mbuf.len, (int) nc->send_mbuf.len, nc->send_mbuf.buf));      break;
      }
      LOG(LL_INFO, ("Message(%db): %.*s", (int) nc->send_mbuf.len, (int) nc->send_mbuf.len, nc->send_mbuf.buf));      break;
    case MG_EV_TIMER:
      LOG(LL_WARN, ("Connect timeout [%0.2fs]", mgos_sys_config_get_discovergy_connection_timeout()));
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      break;
    case MG_EV_SEND:
      LOG(LL_DEBUG, ("Message send"));
      break;
    case MG_EV_HTTP_REPLY:
      //nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      //nc->flags |= MG_F_SEND_AND_CLOSE;
      last_response_time = mgos_uptime() - last_request_start;
      //LOG(LL_DEBUG, ("Response received %.2lfs", last_response_time));
      //LOG(LL_INFO,("Response: %.*s", hm->message.len, hm->message.p));
      if (2 == json_scanf(hm->body.p, hm->body.len, "{ time: %lld, values: { power: %d } }", &last_update, &last_power)) {
        float power = last_power / 1000.0;
        double update = (double) last_update / 1000.0;
        last_lag = mg_time() - update;
        if(callback != NULL) {
          callback(update, power, callback_arg);
        } else { 
          char time[32];
          mgos_strftime(time, 32, "%x %X", (time_t) update);
          LOG(LL_INFO, ("%s[%lld]: %.2f", time, last_update, power));
        }
      } else {
        LOG(LL_ERROR, ("failed to parse json response"));
      }
      //hm->message.len=0;
      break;
    case MG_EV_CLOSE:
      LOG(LL_INFO, ("Server closed connection"));
      connection_count--;
      connection = NULL;
      break;
    default:
      break;
  }
}

static void discovergy_request_handler(void *data) {
  if(!mgos_sys_config_get_discovergy_enable()) {
    LOG(LL_INFO, ("Discovergy API disabled. Skipping request"));
    return;
  }
  //LOG(LL_INFO, ("Server send request (atca enabled %d)\n", mbedtls_atca_is_available()));
  last_request_start = mgos_uptime();
  if(!connection) {
    connection = mg_connect_http(mgos_get_mgr(), discovergy_response_handler, data, url, auth, NULL);
    //mg_set_timer(connection, mg_time() + mgos_sys_config_get_discovergy_connection_timeout());
  } else if(request.len > 0) {
    mg_send(connection, request.buf, request.len);
  } else {
    LOG(LL_INFO, ("connection but no request!"));
    connection == NULL;
  }
}

static void discovergy_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  LOG(LL_DEBUG, ("%.*s crontab job fired!", action.len, action.p));
  discovergy_request_handler(userdata);
}

bool discovergy_init() {

  const struct mgos_config_discovergy *config = mgos_sys_config_get_discovergy();
  if(config == NULL) {
    LOG(LL_ERROR, ("Discovergy config missing in mos.yml"));
    return false;
  }
  if(!config->enable) {
    LOG(LL_INFO, ("Discovergy API disabled."));
    return false;
  }

  struct mbuf buf;
  mbuf_init(&buf, 0);
  mg_basic_auth_header(mg_mk_str(config->user), mg_mk_str(config->password), &buf);
  auth = malloc(sizeof(char) * buf.len+1);
  strncpy(auth, buf.buf, buf.len);
  auth[buf.len] = '\0';
  mbuf_free(&buf);
  LOG(LL_INFO, ("auth %s", auth));

  int len = strlen(urlf)+strlen(config->meter_id);
  url = malloc((len+1)*sizeof(char));
  int n = snprintf(url, len, urlf, config->meter_id);
  if(n < 0 || n >= len) {
    LOG(LL_ERROR, ("Cannot create Discovergy url"));
    return false;
  }

  LOG(LL_INFO, ("url %s", url));
  mbuf_init(&request, 250);

  mgos_prometheus_metrics_add_handler(discovergy_metrics, NULL);
  mgos_crontab_register_handler(mg_mk_str("discovergy"), discovergy_crontab_handler, NULL);

  return true;
}

void discovery_set_update_callback(discovergy_update_callback cb, void *cb_arg) {
  callback = cb;
  callback_arg = cb_arg;
}