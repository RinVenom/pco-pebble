#include "comm.h"
#include "data.h"

// Wait this long for the first (or next) reply message before resending.
#define REPLY_TIMEOUT_MS 20000
#define MAX_ATTEMPTS 4
#define INBOX_SIZE 512
#define OUTBOX_SIZE 256

typedef struct {
  RequestType type;
  char service_type_id[ID_LEN];
  char plan_id[ID_LEN];
  char item_id[ID_LEN];
  bool accept;
} Request;

static CommCallbacks s_callbacks;
static CommHandler s_handler;
static bool s_js_ready;
static bool s_signed_in;
static Request s_req;           // the request in flight (type REQ_NONE when idle)
static int32_t s_req_id;
static int s_attempt;
static bool s_sent_ok;          // outbox acknowledged for this attempt
static AppTimer *s_timer;
static int s_retry_after;

static void send_current(void);

static void notify(RequestType type, CommEvent event, CommError code) {
  if (s_handler) s_handler(type, event, code);
}

static void cancel_timer(void) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
}

static void finish(CommEvent event, CommError code) {
  RequestType type = s_req.type;
  cancel_timer();
  s_req.type = REQ_NONE;
  notify(type, event, code);
}

static void retry_timer_cb(void *ctx) {
  s_timer = NULL;
  if (s_req.type == REQ_NONE) return;
  if (!comm_phone_connected()) { finish(COMM_ERROR, ERR_NO_PHONE); return; }
  if (s_attempt >= MAX_ATTEMPTS) { finish(COMM_ERROR, ERR_TIMEOUT); return; }
  send_current();
}

// Backoff: 1 s, 2 s, 4 s.
static void schedule_retry(void) {
  cancel_timer();
  int delay = 1000 << (s_attempt > 0 ? s_attempt - 1 : 0);
  s_timer = app_timer_register(delay, retry_timer_cb, NULL);
}

static void reply_timeout_cb(void *ctx) {
  s_timer = NULL;
  if (s_req.type == REQ_NONE) return;
  APP_LOG(APP_LOG_LEVEL_WARNING, "Request %d timed out (attempt %d)", s_req.type, s_attempt);
  retry_timer_cb(NULL);
}

static void restart_reply_timer(void) {
  cancel_timer();
  s_timer = app_timer_register(REPLY_TIMEOUT_MS, reply_timeout_cb, NULL);
}

static void send_current(void) {
  if (s_req.type == REQ_NONE || !s_js_ready) return;
  DictionaryIterator *iter;
  AppMessageResult r = app_message_outbox_begin(&iter);
  s_attempt++;
  s_sent_ok = false;
  if (r != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "outbox_begin failed: %d", (int)r);
    schedule_retry();
    return;
  }
  s_req_id++;
  dict_write_int32(iter, MESSAGE_KEY_Request, (int32_t)s_req.type);
  dict_write_int32(iter, MESSAGE_KEY_ReqId, s_req_id);
  if (s_req.service_type_id[0]) dict_write_cstring(iter, MESSAGE_KEY_ServiceTypeId, s_req.service_type_id);
  if (s_req.plan_id[0]) dict_write_cstring(iter, MESSAGE_KEY_PlanId, s_req.plan_id);
  if (s_req.item_id[0]) dict_write_cstring(iter, MESSAGE_KEY_ItemId, s_req.item_id);
  if (s_req.type == REQ_RESPOND) dict_write_int32(iter, MESSAGE_KEY_Accept, s_req.accept ? 1 : 0);
  if (s_req.type == REQ_PLANS) {
    dict_write_int32(iter, MESSAGE_KEY_Count, MAX_PLANS);
    if (g_data.last_service_type[0]) dict_write_cstring(iter, MESSAGE_KEY_LastServiceType, g_data.last_service_type);
  } else if (s_req.type == REQ_ITEMS) {
    dict_write_int32(iter, MESSAGE_KEY_Count, MAX_ITEMS);
  } else if (s_req.type == REQ_REQUESTS) {
    dict_write_int32(iter, MESSAGE_KEY_Count, MAX_REQUESTS);
  }
  r = app_message_outbox_send();
  if (r != APP_MSG_OK) {
    schedule_retry();
    return;
  }
  restart_reply_timer();
}

void comm_request(RequestType type, const char *service_type_id, const char *plan_id,
                  const char *item_id, bool accept) {
  cancel_timer();
  memset(&s_req, 0, sizeof(s_req));
  s_req.type = type;
  data_copy(s_req.service_type_id, service_type_id, ID_LEN);
  data_copy(s_req.plan_id, plan_id, ID_LEN);
  data_copy(s_req.item_id, item_id, ID_LEN);
  s_req.accept = accept;
  s_attempt = 0;
  s_retry_after = 0;

  if (!comm_phone_connected()) { finish(COMM_ERROR, ERR_NO_PHONE); return; }
  if (!s_js_ready) {
    // Sent when JSReady arrives. Time out if it never does.
    restart_reply_timer();
    return;
  }
  send_current();
}

void comm_cancel(void) {
  cancel_timer();
  s_req.type = REQ_NONE;
}

// ---- Incoming ----------------------------------------------------------

static const char *str_of(DictionaryIterator *iter, uint32_t key) {
  Tuple *t = dict_find(iter, key);
  return (t && t->type == TUPLE_CSTRING) ? t->value->cstring : "";
}

static bool int_of(DictionaryIterator *iter, uint32_t key, int32_t *out) {
  Tuple *t = dict_find(iter, key);
  if (!t) return false;
  switch (t->type) {
    case TUPLE_INT:
    case TUPLE_UINT:
      if (t->length == 1) *out = (t->type == TUPLE_INT) ? t->value->int8 : t->value->uint8;
      else if (t->length == 2) *out = (t->type == TUPLE_INT) ? t->value->int16 : t->value->uint16;
      else *out = t->value->int32;
      return true;
    default:
      return false;
  }
}

// Stores one list row. Returns true when the list is complete.
static bool store_row(DictionaryIterator *iter) {
  int32_t index = 0, count = 0;
  int_of(iter, MESSAGE_KEY_Index, &index);
  int_of(iter, MESSAGE_KEY_Count, &count);

  switch (s_req.type) {
    case REQ_PLANS:
      if (index == 0) { g_data.plan_count = 0; }
      if (count > 0 && index < MAX_PLANS && index == g_data.plan_count) {
        PlanRow *p = &g_data.plans[g_data.plan_count++];
        data_copy(p->section, str_of(iter, MESSAGE_KEY_Section), SECTION_LEN);
        data_copy(p->service_type_id, str_of(iter, MESSAGE_KEY_ServiceTypeId), ID_LEN);
        data_copy(p->plan_id, str_of(iter, MESSAGE_KEY_PlanId), ID_LEN);
        data_copy(p->title, str_of(iter, MESSAGE_KEY_Title), TITLE_LEN);
        data_copy(p->subtitle, str_of(iter, MESSAGE_KEY_Subtitle), SUBTITLE_LEN);
        int32_t status = STATUS_NONE;
        int_of(iter, MESSAGE_KEY_Status, &status);
        p->status = (status >= STATUS_NONE && status <= STATUS_DECLINED) ? (uint8_t)status : STATUS_NONE;
        p->time = 0;
        int_of(iter, MESSAGE_KEY_Time, &p->time);
      }
      if (index + 1 >= count) {
        g_data.plans_updated = time(NULL);
        g_data.plans_from_cache = false;
        return true;
      }
      return false;

    case REQ_ITEMS:
      if (index == 0) {
        g_data.item_count = 0;
        data_copy(g_data.items_plan_id, s_req.plan_id, ID_LEN);
      }
      if (count > 0 && index < MAX_ITEMS && index == g_data.item_count) {
        ItemRow *it = &g_data.items[g_data.item_count++];
        int32_t kind = 0;
        int_of(iter, MESSAGE_KEY_Kind, &kind);
        it->kind = (uint8_t)kind;
        data_copy(it->id, str_of(iter, MESSAGE_KEY_ItemId), ID_LEN);
        data_copy(it->title, str_of(iter, MESSAGE_KEY_Title), TITLE_LEN);
        data_copy(it->subtitle, str_of(iter, MESSAGE_KEY_Subtitle), SUBTITLE_LEN);
      }
      if (index + 1 >= count) { g_data.items_loaded = true; return true; }
      return false;

    case REQ_REQUESTS:
      if (index == 0) g_data.request_count = 0;
      if (count > 0 && index < MAX_REQUESTS && index == g_data.request_count) {
        RequestRow *r = &g_data.requests[g_data.request_count++];
        data_copy(r->id, str_of(iter, MESSAGE_KEY_ItemId), ID_LEN);
        data_copy(r->title, str_of(iter, MESSAGE_KEY_Title), TITLE_LEN);
        data_copy(r->subtitle, str_of(iter, MESSAGE_KEY_Subtitle), SUBTITLE_LEN);
      }
      if (index + 1 >= count) { g_data.requests_loaded = true; return true; }
      return false;

    default:
      return true;
  }
}

static void store_live(DictionaryIterator *iter) {
  LiveState *l = &g_data.live;
  int32_t state = 0, elapsed = 0, length = 0;
  int_of(iter, MESSAGE_KEY_LiveState, &state);
  int_of(iter, MESSAGE_KEY_LiveElapsed, &elapsed);
  int_of(iter, MESSAGE_KEY_LiveLength, &length);
  time_t now = time(NULL);
  l->has_data = true;
  l->live = state == 1;
  data_copy(l->current, str_of(iter, MESSAGE_KEY_LiveCurTitle), TITLE_LEN);
  data_copy(l->next, str_of(iter, MESSAGE_KEY_LiveNextTitle), TITLE_LEN);
  // The phone sends elapsed seconds, so phone/watch clock skew does not matter.
  l->item_started = now - elapsed;
  l->item_length = length;
  l->last_update = now;
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  int32_t value;

  // Messages that are not answers to a request.
  if (int_of(iter, MESSAGE_KEY_VibeOnChange, &value)) {
    g_data.vibe_on_change = value != 0;
  }
  bool has_auth = int_of(iter, MESSAGE_KEY_AuthState, &value);
  bool signed_in = has_auth && value != 0;
  if (dict_find(iter, MESSAGE_KEY_JSReady)) {
    s_js_ready = true;
    s_signed_in = signed_in;
    if (s_callbacks.on_ready) s_callbacks.on_ready(signed_in);
    if (s_req.type != REQ_NONE && signed_in) send_current();
    return;
  }
  if (has_auth) {
    bool changed = signed_in != s_signed_in;
    s_signed_in = signed_in;
    if (changed && s_callbacks.on_auth_changed) s_callbacks.on_auth_changed(signed_in);
  }
  if (dict_find(iter, MESSAGE_KEY_PlansChanged)) {
    if (s_signed_in && s_callbacks.on_plans_changed) s_callbacks.on_plans_changed();
    return;
  }

  // Answers: ignore anything for an older request.
  int32_t req_id;
  if (!int_of(iter, MESSAGE_KEY_ReqId, &req_id)) return;
  if (req_id != s_req_id || s_req.type == REQ_NONE) return;

  if (int_of(iter, MESSAGE_KEY_Error, &value)) {
    int32_t ra = 0;
    int_of(iter, MESSAGE_KEY_RetryAfter, &ra);
    s_retry_after = ra;
    APP_LOG(APP_LOG_LEVEL_WARNING, "Request %d error %d", s_req.type, (int)value);
    finish(COMM_ERROR, (CommError)value);
    return;
  }

  if (s_req.type == REQ_LIVE) {
    store_live(iter);
    finish(COMM_DONE, ERR_NONE);
  } else if (s_req.type == REQ_RESPOND) {
    finish(COMM_DONE, ERR_NONE);
  } else {
    RequestType type = s_req.type;
    if (store_row(iter)) finish(COMM_DONE, ERR_NONE);
    else { restart_reply_timer(); notify(type, COMM_PROGRESS, ERR_NONE); }
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Inbox dropped: %d", (int)reason);
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  s_sent_ok = true;
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox failed: %d", (int)reason);
  if (s_req.type != REQ_NONE && !s_sent_ok) schedule_retry();
}

static void connection_handler(bool connected) {
  if (!connected) {
    // A fresh JS context starts on reconnect and sends JSReady again.
    s_js_ready = false;
    if (s_req.type != REQ_NONE) finish(COMM_ERROR, ERR_NO_PHONE);
  }
  if (s_callbacks.on_connection_changed) s_callbacks.on_connection_changed(connected);
}

void comm_init(CommCallbacks callbacks) {
  s_callbacks = callbacks;
  // Register every callback before app_message_open().
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);
  // Fixed small buffers: one row per message, and the heap is needed for
  // menus (the maximum sizes would take about 8 KB each).
  app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = connection_handler,
  });
}

void comm_deinit(void) {
  cancel_timer();
  connection_service_unsubscribe();
  app_message_deregister_callbacks();
}

void comm_set_handler(CommHandler handler) { s_handler = handler; }
bool comm_is_ready(void) { return s_js_ready; }
bool comm_is_busy(void) { return s_req.type != REQ_NONE; }
bool comm_phone_connected(void) { return connection_service_peek_pebble_app_connection(); }
int comm_retry_after(void) { return s_retry_after; }
RequestType comm_current(void) { return s_req.type; }
