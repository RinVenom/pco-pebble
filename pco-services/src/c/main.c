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

static void deinit(void) {
  data_save();
  comm_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
