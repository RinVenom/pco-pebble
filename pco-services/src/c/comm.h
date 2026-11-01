#pragma once
#include <pebble.h>

// Request types. Must match REQ in src/pkjs/index.js.
typedef enum {
  REQ_NONE = 0,
  REQ_PLANS = 1,
  REQ_ITEMS = 2,
  REQ_LIVE = 3,
  REQ_REQUESTS = 4,
  REQ_RESPOND = 5,
} RequestType;

// Error codes. 1-5 match ERR in src/pkjs/auth.js; the rest are watch-side.
typedef enum {
  ERR_NONE = 0,
  ERR_AUTH = 1,
  ERR_NETWORK = 2,
  ERR_RATE_LIMIT = 3,
  ERR_SERVER = 4,
  ERR_NOT_FOUND = 5,
  ERR_NO_PHONE = 10,
  ERR_TIMEOUT = 11,
} CommError;

typedef enum {
  COMM_PROGRESS,  // a list row arrived
  COMM_DONE,      // the request finished
  COMM_ERROR,     // the request failed; see code
} CommEvent;

// The screen on top registers one handler and gets events for its requests.
typedef void (*CommHandler)(RequestType type, CommEvent event, CommError code);

// Called by main.c for messages that are not answers to a request.
typedef struct {
  void (*on_ready)(bool signed_in);
  void (*on_auth_changed)(bool signed_in);
  void (*on_connection_changed)(bool connected);
  void (*on_plans_changed)(void);  // settings changed which plans to show
} CommCallbacks;

void comm_init(CommCallbacks callbacks);
void comm_deinit(void);
void comm_set_handler(CommHandler handler);

bool comm_is_ready(void);
bool comm_is_busy(void);
bool comm_phone_connected(void);

// Sends a request. A newer request replaces one that is still running.
// ids may be NULL. accept is used by REQ_RESPOND only.
void comm_request(RequestType type, const char *service_type_id, const char *plan_id,
                  const char *item_id, bool accept);
void comm_cancel(void);

// Seconds the phone asked to wait after HTTP 429 (0 if none).
int comm_retry_after(void);

// The request in flight, or REQ_NONE.
RequestType comm_current(void);
