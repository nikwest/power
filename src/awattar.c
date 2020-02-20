#include "awattar.h"

#include "mgos_crontab.h"
#include "mgos_prometheus_metrics.h"

#define PRICE_ARRAY_SIZE 24

static const char *url = "https://api.awattar.de/v1/marketdata";

static awattar_update_callback callback = NULL;
static void *callback_arg;

static awattar_pricing_t entries[PRICE_ARRAY_SIZE];
static int entries_count = 0;
static int retry_counter = 0;

static void awattar_request_handler(void *data);
static void awattar_response_handler(struct mg_connection *nc, int ev, void *ev_data, void *ud);
static void retry_request();

static void awattar_metrics(struct mg_connection *nc, void *data) {
    mgos_prometheus_metrics_printf(
        nc, GAUGE, "awattar_retries", "Number of request retries until successful",
        "%d", retry_counter
    );
    (void) data;
}

static void retry_request() {
  mgos_set_timer(60000 * retry_counter /* ms */, 0, awattar_request_handler, NULL);
  retry_counter++;
}

static void scan_array(const char *str, int len, void *user_data) {
    struct json_token t;
    int i;
    float price;
    int64_t start, end;
//    char time[32];

    for (i = 0; i<PRICE_ARRAY_SIZE && json_scanf_array_elem(str, len, "", i, &t) > 0; i++) {
      json_scanf(t.ptr, t.len, "{start_timestamp: %lld, end_timestamp: %lld, marketprice: %f", &start, &end, &price);
      entries[i].start = start / 1e3;
      entries[i].end = end / 1e3;
      entries[i].price = price / 1e3;
      entries_count = i;
      // mgos_strftime(time, 32, "%x %X", entries[i].start);
      // LOG(LL_INFO,("%s[%.0fmin]: %.4f", time, (entries[i].end - entries[i].start)/60.0, entries[i].price));
    }

    (void) user_data;
}

static void awattar_response_handler(struct mg_connection *nc, int ev, void *ev_data, void *ud) {
  struct http_message *hm = (struct http_message *) ev_data;
  switch (ev) {
    case MG_EV_CONNECT:
      if (*(int *) ev_data != 0) {
        LOG(LL_ERROR, ("connect() failed[%d]: %s\n", (*(int *) ev_data), url));
        retry_request();
      }
      break;
    case MG_EV_HTTP_REPLY:
      //nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      // LOG(LL_INFO,("Response: %.*s", hm->body.len, hm->body.p));
      if (1 != json_scanf(hm->body.p, hm->body.len, "{ data: %M }", &scan_array)) {
        LOG(LL_ERROR, ("failed to parse json response\n"));
        retry_request();
        break;
      } 
      LOG(LL_INFO, ("Successfully parsed market data\n"));
      retry_counter = 0;
      if(callback != NULL) {
        callback(entries, entries_count, callback_arg);
      }
      mbuf_remove(&nc->recv_mbuf, nc->recv_mbuf.len);
      break;
    case MG_EV_CLOSE:
      LOG(LL_INFO, ("Server closed connection\n"));
      break;
    default:
      break;
  }

  (void) ud;
}

static void got_ip_handler(int ev, void *evd, void *data) {
  if (ev != MGOS_NET_EV_IP_ACQUIRED) {
    return;
  }
  awattar_request_handler(data);

  (void) evd;
}

static void awattar_request_handler(void *data) {
  LOG(LL_INFO, ("Server send request\n"));
  mg_connect_http(mgos_get_mgr(), awattar_response_handler, data, url, NULL, NULL);
}

static void awattar_crontab_handler(struct mg_str action,
                      struct mg_str payload, void *userdata) {
  LOG(LL_DEBUG, ("%.*s crontab job fired!", action.len, action.p));
  awattar_request_handler(NULL);

  (void) payload;  
  (void) userdata;
}

bool awattar_init() {
  mgos_crontab_register_handler(mg_mk_str("awattar"), awattar_crontab_handler, NULL);
  mgos_prometheus_metrics_add_handler(awattar_metrics, NULL);

  //mgos_set_timer(60000 /* ms */, 0, awattar_request_handler, NULL);
  mgos_event_add_handler(MGOS_NET_EV_IP_ACQUIRED, got_ip_handler, NULL);

  return true;
}

void awattar_set_update_callback(awattar_update_callback cb, void *cb_arg) {
  callback = cb;
  callback_arg = cb_arg;
}

int awattar_get_entries_count() {
  return entries_count;
}

awattar_pricing_t* awattar_get_entries() {
  return entries;
}

awattar_pricing_t* awattar_get_entry(time_t time) {
  int entries_count = awattar_get_entries_count();
  awattar_pricing_t *ptr = awattar_get_entries();
  for(int i = 0; i < entries_count; i++) {
    if(ptr->start <= time && ptr->end > time) {
      return ptr;
    }
    ptr++;
  }
  retry_request();
  return NULL;
}


awattar_pricing_t* awattar_get_best_entry(time_t after) {
  int entries_count = awattar_get_entries_count();
  if(entries_count == 0) {
    retry_request();
    return NULL;
  }
  awattar_pricing_t *ptr = awattar_get_entries();
  awattar_pricing_t *max = ptr;
  int i = 1;
  // char time[32];
  while(i < entries_count) {
    if(ptr->end < after) {
      ptr++; i++; max++;
      LOG(LL_INFO, ("End of scheduled power out is in the past, ignoring"));
      continue;
    }
    // mgos_strftime(time, 32, "%x %X", ptr->start);
    // LOG(LL_INFO, ("Time: %s[%.0fmin]: %.4f", time, (ptr->end - ptr->start)/60.0, ptr->price));
    if(ptr->price > max->price) {
      max = ptr;
    }
    ptr++; i++;
  }
  // mgos_strftime(time, 32, "%x %X", max->start);
  // LOG(LL_INFO, ("Best time: %s[%.0fmin]: %.4f", time, (max->end - max->start)/60.0, max->price));
  return max;
}
