#include "awattar.h"


static const char *url = "https://api.awattar.de/v1/marketdata";

static void scan_array(const char *str, int len, void *user_data) {
    struct json_token t;
    int i;
    float price;
    int64_t start, end;
    stockprice_t entry;
    char time[32];

    for (i = 0; json_scanf_array_elem(str, len, "", i, &t) > 0; i++) {
      json_scanf(t.ptr, t.len, "{start_timestamp: %lld, end_timestamp: %lld, marketprice: %f", &start, &end, &price);
      entry.start = start / 1000;
      entry.end = end / 1000;
      entry.price = price / 1000.0;
      mgos_strftime(time, 32, "%x %X", entry.start);
      LOG(LL_INFO, ("%s[%.0fmin]: %.4f", time, (entry.end-entry.start)/60.0, entry.price));
    }
}

static void awattar_response_handler(struct mg_connection *nc, int ev, void *ev_data, void *ud) {
  struct http_message *hm = (struct http_message *) ev_data;
  switch (ev) {
    case MG_EV_CONNECT:
      if (*(int *) ev_data != 0) {
        LOG(LL_ERROR, ("connect() failed[%d]: %s\n", (*(int *) ev_data), url));
      }
      break;
    case MG_EV_HTTP_REPLY:
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      //LOG(LL_INFO, (hm->body.p));
      if (1 == json_scanf(hm->body.p, hm->body.len, "{ data: %M }", &scan_array)) {
        LOG(LL_INFO, ("Successfully parsed market data\n"));
      } else {
        LOG(LL_ERROR, ("failed to parse json response\n"));
      }
      break;
    case MG_EV_CLOSE:
      LOG(LL_INFO, ("Server closed connection\n"));
      break;
    default:
      break;
  }
}

static void awattar_request_handler(void *data) {
  LOG(LL_INFO, ("Server send request\n"));
  mg_connect_http(mgos_get_mgr(), awattar_response_handler, data, url, NULL, NULL);
}


bool awattar_init() {
  awattar_request_handler(NULL);

  return true;
}