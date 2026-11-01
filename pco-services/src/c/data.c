#include "data.h"

AppData g_data;

// Persistent storage keys. Bump STORAGE_VERSION when a stored layout changes.
#define STORAGE_VERSION 1
#define KEY_VERSION 1
#define KEY_PLAN_COUNT 2
#define KEY_PLANS_UPDATED 3
#define KEY_LAST_SERVICE_TYPE 4
#define KEY_VIBE 5
#define KEY_PLAN_BASE 100  // KEY_PLAN_BASE + i holds plans[i]


void data_copy(char *dst, const char *src, size_t n) {
  if (!dst || n == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  size_t len = strlen(src);
  if (len >= n) {
    len = n - 1;
    // Step back over UTF-8 continuation bytes (10xxxxxx) so a character is
    // never cut in half, then drop the lead byte of that character.
    while (len > 0 && ((uint8_t)src[len] & 0xC0) == 0x80) len--;
  }
  memcpy(dst, src, len);
  dst[len] = '\0';
}

static void delete_plan_keys(int from, int to) {
  for (int i = from; i < to; i++) {
    persist_delete(KEY_PLAN_BASE + i);
  }
}

void data_load(void) {
  memset(&g_data, 0, sizeof(g_data));

  int version = persist_exists(KEY_VERSION) ? persist_read_int(KEY_VERSION) : 0;
  if (version != STORAGE_VERSION) {
    // Unknown or old layout: start clean.
    persist_delete(KEY_PLAN_COUNT);
    persist_delete(KEY_PLANS_UPDATED);
    delete_plan_keys(0, 64);
    persist_write_int(KEY_VERSION, STORAGE_VERSION);
  }

  if (persist_exists(KEY_LAST_SERVICE_TYPE)) {
    persist_read_string(KEY_LAST_SERVICE_TYPE, g_data.last_service_type, ID_LEN);
  }
  g_data.vibe_on_change = persist_exists(KEY_VIBE) ? persist_read_bool(KEY_VIBE) : false;

  int count = persist_exists(KEY_PLAN_COUNT) ? persist_read_int(KEY_PLAN_COUNT) : 0;
  if (count < 0) count = 0;
  if (count > MAX_PLANS) count = MAX_PLANS;
  int loaded = 0;
  for (int i = 0; i < count; i++) {
    if (persist_read_data(KEY_PLAN_BASE + i, &g_data.plans[loaded], sizeof(PlanRow)) == (int)sizeof(PlanRow)) {
      g_data.plans[loaded].title[TITLE_LEN - 1] = '\0';
      g_data.plans[loaded].subtitle[SUBTITLE_LEN - 1] = '\0';
      g_data.plans[loaded].section[SECTION_LEN - 1] = '\0';
      loaded++;
    }
  }
  g_data.plan_count = loaded;
  if (loaded > 0) {
    g_data.plans_updated = persist_read_int(KEY_PLANS_UPDATED);
    g_data.plans_from_cache = true;
  }
}

void data_save(void) {
  // Only plans that came from the phone in this session are written.
  if (!g_data.plans_from_cache && g_data.plans_updated != 0) {
    int old = persist_exists(KEY_PLAN_COUNT) ? persist_read_int(KEY_PLAN_COUNT) : 0;
    for (int i = 0; i < g_data.plan_count; i++) {
      persist_write_data(KEY_PLAN_BASE + i, &g_data.plans[i], sizeof(PlanRow));
    }
    if (old > g_data.plan_count) delete_plan_keys(g_data.plan_count, old);
    persist_write_int(KEY_PLAN_COUNT, g_data.plan_count);
    persist_write_int(KEY_PLANS_UPDATED, (int32_t)g_data.plans_updated);
  }
  persist_write_string(KEY_LAST_SERVICE_TYPE, g_data.last_service_type);
  persist_write_bool(KEY_VIBE, g_data.vibe_on_change);
}

void data_clear_account(void) {
  int old = persist_exists(KEY_PLAN_COUNT) ? persist_read_int(KEY_PLAN_COUNT) : 0;
  delete_plan_keys(0, old > MAX_PLANS ? old : MAX_PLANS);
  persist_delete(KEY_PLAN_COUNT);
  persist_delete(KEY_PLANS_UPDATED);
  persist_delete(KEY_LAST_SERVICE_TYPE);
  g_data.plan_count = 0;
  g_data.plans_updated = 0;
  g_data.plans_from_cache = false;
  g_data.item_count = 0;
  g_data.items_loaded = false;
  g_data.items_plan_id[0] = '\0';
  g_data.request_count = 0;
  g_data.requests_loaded = false;
  g_data.last_service_type[0] = '\0';
  memset(&g_data.live, 0, sizeof(g_data.live));
}

void data_format_age(time_t when, char *buf, size_t n) {
  if (when == 0) { snprintf(buf, n, "never"); return; }
  int32_t age = (int32_t)(time(NULL) - when);
  if (age < 60) snprintf(buf, n, "just now");
  else if (age < 3600) snprintf(buf, n, "%dm ago", (int)(age / 60));
  else if (age < 86400) snprintf(buf, n, "%dh ago", (int)(age / 3600));
  else snprintf(buf, n, "%dd ago", (int)(age / 86400));
}
