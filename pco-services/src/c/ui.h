#pragma once
#include <pebble.h>
#include "data.h"

#define ACCENT_COLOR PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack)
#define MUTED_COLOR PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
#define ALERT_COLOR PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)
#define TEXT_INSET PBL_IF_ROUND_ELSE(18, 6)
#define UI_HEADER_HEIGHT 22

void ui_draw_header(GContext *ctx, const Layer *cell, const char *text);

// Full-screen messages. One is shown at a time.
typedef enum {
  MSG_SIGN_IN,     // no token: tell the user to open the app settings
  MSG_NO_PHONE,    // Bluetooth link to the phone is down
  MSG_NO_INTERNET, // phone is connected but cannot reach Planning Center
  MSG_RESULT,      // short result dialog (auto closes)
} MessageKind;

void message_show(MessageKind kind, const char *title, const char *body, uint32_t auto_close_ms);
void message_hide(MessageKind kind);
bool message_is_showing(MessageKind kind);

// Main menu: upcoming plans by service type.
void plans_window_push(void);
Window *plans_window_get(void);
void plans_window_refresh(void);
void plans_window_set_waiting(bool waiting);

// Plan order.
void items_window_push(const PlanRow *plan);

// Live view.
void live_window_push(const char *service_type_id, const char *plan_id);

// Scheduling requests.
void requests_window_push(void);
