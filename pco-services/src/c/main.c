#include <pebble.h>
#include "comm.h"
#include "data.h"
#include "ui.h"

// Startup: show cached plans at once, wait for JSReady from the phone,
// then either ask the user to sign in or refresh the plans.

#define JS_READY_WAIT_MS 8000

static AppTimer *s_ready_timer;

static void show_sign_in(void) {
  message_show(MSG_SIGN_IN, "Sign in",
               "On your phone, open the Pebble app, find PCO Services and tap Settings to sign in to Planning Center.",
               0);
}

static void show_no_phone(void) {
  message_show(MSG_NO_PHONE, "No phone",
               "Your watch is not connected to your phone. Plans shown are from your last update.", 0);
}

// Removes every window above the main menu.
static void pop_to_plans(void) {
  Window *plans = plans_window_get();
  while (window_stack_get_top_window() && window_stack_get_top_window() != plans) {
    window_stack_pop(false);
  }
}

static void on_ready(bool signed_in) {
  if (s_ready_timer) { app_timer_cancel(s_ready_timer); s_ready_timer = NULL; }
  plans_window_set_waiting(false);
  message_hide(MSG_NO_PHONE);
  if (signed_in) {
    message_hide(MSG_SIGN_IN);
    plans_window_refresh();
  } else {
    comm_cancel();
    data_clear_account();
    pop_to_plans();
    plans_window_set_waiting(false);
    show_sign_in();
  }
}

static void on_auth_changed(bool signed_in) {
  if (signed_in) {
    message_hide(MSG_SIGN_IN);
    pop_to_plans();
    plans_window_refresh();
  } else {
    // Signed out on the phone: drop this account's cached data.
    data_clear_account();
    pop_to_plans();
    plans_window_set_waiting(false);
    show_sign_in();
  }
}

static void on_connection_changed(bool connected) {
  if (connected) {
    message_hide(MSG_NO_PHONE);
    plans_window_set_waiting(true);  // JSReady follows from the new JS context
  } else if (!message_is_showing(MSG_SIGN_IN)) {
    show_no_phone();
  }
}

// The service type filter changed on the phone. Shows the new list on the
// main menu (or when the user returns to it).
static void on_plans_changed(void) {
  plans_window_refresh();
}

static void ready_timeout(void *ctx) {
  s_ready_timer = NULL;
  if (comm_is_ready()) return;
  plans_window_set_waiting(false);
  if (!comm_phone_connected()) show_no_phone();
}

static void init(void) {
#if defined(PBL_TOUCH)
  // Time 2 and Round 2: let the system drive menus and the action bar by
  // touch (tap a row to open it, swipe to scroll, tap the accept/decline
  // icons). It only acts after the user wakes the watch or presses a button.
  app_touch_navigation_enable(true);
#endif
  data_load();
  comm_init((CommCallbacks) {
    .on_ready = on_ready,
    .on_auth_changed = on_auth_changed,
    .on_connection_changed = on_connection_changed,
    .on_plans_changed = on_plans_changed,
  });
  plans_window_push();

  if (!comm_phone_connected()) {
    show_no_phone();
  } else {
    plans_window_set_waiting(true);
    s_ready_timer = app_timer_register(JS_READY_WAIT_MS, ready_timeout, NULL);
  }
}

#if defined(_PBL_API_EXISTS_app_glance_reload)  // = PBL_API_EXISTS(app_glance_reload), without its warning
// App glance: the line under "PCO Services" in the watch's app list. One
// slice per upcoming plan the user is scheduled on (accepted or still to
// answer), soonest first. Each slice lasts until 4 hours after its plan
// starts, so the glance moves to the next service by itself. Declined and
// unscheduled plans are left out; with none, the glance is empty.
#define GLANCE_KEEP_SECONDS (4 * 3600)

static void glance_reload(AppGlanceReloadSession *session, size_t limit, void *ctx) {
  time_t now = time(NULL);
  bool used[MAX_PLANS] = { false };
  for (size_t added = 0; added < limit; added++) {
    // Next soonest plan not shown yet.
    int best = -1;
    for (int i = 0; i < g_data.plan_count; i++) {
      PlanRow *p = &g_data.plans[i];
      if (used[i] || p->time <= 0) continue;
      if (p->status != STATUS_ACCEPTED && p->status != STATUS_REQUESTED) continue;
      if ((time_t)p->time + GLANCE_KEEP_SECONDS <= now) continue;
      if (best < 0 || p->time < g_data.plans[best].time) best = i;
    }
    if (best < 0) return;
    used[best] = true;
    PlanRow *p = &g_data.plans[best];
    char text[TITLE_LEN + SECTION_LEN + 16];
    snprintf(text, sizeof(text), "%s%s · %s",
             p->status == STATUS_REQUESTED ? "Reply: " : "", p->title, p->section);
    AppGlanceSlice slice = {
      .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = text },
      .expiration_time = (time_t)p->time + GLANCE_KEEP_SECONDS,
    };
    AppGlanceResult r = app_glance_add_slice(session, slice);
    if (r != APP_GLANCE_RESULT_SUCCESS) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "App glance slice %d failed: %d", (int)added, (int)r);
      return;
    }
  }
}
#endif

static void deinit(void) {
  data_save();
#if defined(_PBL_API_EXISTS_app_glance_reload)  // = PBL_API_EXISTS(app_glance_reload), without its warning
  app_glance_reload(glance_reload, NULL);
#endif
  comm_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
