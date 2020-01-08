
#include "discovergy.h"

#include "mgos.h"
#include "mgos_mongoose.h"

static const char *urlf = "https://api.discovergy.com/public/v1/last_reading?fields=power&meterId=%s";
static char *url = NULL;
static char *auth;

static void discovergy_response_handler(struct mg_connection *nc, int ev, void *ev_data, void *ud) {
  struct http_message *hm = (struct http_message *) ev_data;
  switch (ev) {
    case MG_EV_CONNECT:
      if (*(int *) ev_data != 0) {
        LOG(LL_ERROR, ("connect() failed[%d]: %s\n", (*(int *) ev_data), url));
      }
      break;
    case MG_EV_HTTP_REPLY:
      LOG(LL_INFO, (hm->body.p));
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      break;
    case MG_EV_CLOSE:
      LOG(LL_INFO, ("Server closed connection\n"));
      break;
    default:
      break;
  }
}

static void discovergy_request_handler(void *data) {
  mg_connect_http(mgos_get_mgr(), discovergy_response_handler, NULL, url, auth, NULL);
}

bool discovergy_init() {

  const struct mgos_config_discovergy *config = mgos_sys_config_get_discovergy();
  if( config == NULL) {
    LOG(LL_ERROR, ("Discovergy config missing in mos.yml"));
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

  if( config->interval > 0) {
    mgos_set_timer(config->interval * 1000 /* ms */, MGOS_TIMER_REPEAT, discovergy_request_handler, NULL);
  }

  return true;
}