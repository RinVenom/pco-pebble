#pragma once
#include <pebble.h>

// String buffer sizes (bytes, including the terminator). index.js cuts
// strings to fit; data_copy() also cuts at a UTF-8 boundary.
#define ID_LEN 16
#define SECTION_LEN 24
#define TITLE_LEN 40
#define SUBTITLE_LEN 40

// List capacities. Aplite has 24 KB for code and heap together.
#if defined(PBL_PLATFORM_APLITE)
#define MAX_PLANS 8
#define MAX_ITEMS 16
#define MAX_REQUESTS 4
#else
#define MAX_PLANS 24
#define MAX_ITEMS 60
#define MAX_REQUESTS 12
#endif

typedef enum {
  KIND_ITEM = 0,
  KIND_SONG = 1,
  KIND_HEADER = 2,
  KIND_MEDIA = 3,
} ItemKind;

// The user's schedule status for a plan (same numbers in pco.js).
typedef enum {
  STATUS_NONE = 0,       // not scheduled: no icon
  STATUS_REQUESTED = 1,  // asked, not answered yet
  STATUS_ACCEPTED = 2,
  STATUS_DECLINED = 3,
} PlanStatus;

typedef struct {
  char section[SECTION_LEN];
  char service_type_id[ID_LEN];
  char plan_id[ID_LEN];
  char title[TITLE_LEN];
  char subtitle[SUBTITLE_LEN];
  uint8_t status;  // PlanStatus
} PlanRow;  // 137 bytes, under PERSIST_DATA_MAX_LENGTH (256)

typedef struct {
  char id[ID_LEN];
  char title[TITLE_LEN];
  char subtitle[SUBTITLE_LEN];
  uint8_t kind;
} ItemRow;

typedef struct {
  char id[ID_LEN];
  char title[TITLE_LEN];
  char subtitle[SUBTITLE_LEN];
} RequestRow;

typedef struct {
  bool has_data;
  bool live;              // false: the service has not started
  char current[TITLE_LEN];
  char next[TITLE_LEN];
  time_t item_started;    // watch time when the current item started
  int32_t item_length;    // seconds, 0 if unknown
  time_t last_update;     // watch time of the last good poll
} LiveState;

typedef struct {
  PlanRow plans[MAX_PLANS];
  int plan_count;
  time_t plans_updated;   // 0 = never loaded
  bool plans_from_cache;

  char items_plan_id[ID_LEN];
  ItemRow items[MAX_ITEMS];
  int item_count;
  bool items_loaded;

  RequestRow requests[MAX_REQUESTS];
  int request_count;
  bool requests_loaded;

  LiveState live;

  char last_service_type[ID_LEN];
  bool vibe_on_change;
} AppData;

extern AppData g_data;

// Copies src into dst (size n) without splitting a UTF-8 character.
void data_copy(char *dst, const char *src, size_t n);

void data_load(void);   // at launch
void data_save(void);   // at exit
void data_clear_account(void);  // after sign-out: drop every cached plan

// Writes "just now", "5m ago", "3h ago" or "2d ago".
void data_format_age(time_t when, char *buf, size_t n);
