#include "ui.h"
#include "comm.h"

// Live view. The watch drives polling with an AppTimer (PebbleKit JS timers
// stop on iOS while the phone app is in the background) and counts the item
// clock locally between polls.

#define POLL_MS 5000
#define MAX_BACKOFF_MS 60000
#define STALE_SECONDS 20
#define CLOCK_BAND_TOP 6  // black-and-white over-time band: offset into the clock row (digits start lower)

static Window *s_window;
static StatusBarLayer *s_status_bar;
static Layer *s_canvas;
static AppTimer *s_poll_timer;
static char s_service_type_id[ID_LEN];
static char s_plan_id[ID_LEN];
static uint32_t s_backoff_ms;
static CommError s_error;
static bool s_visible;

static void poll(void *ctx);

static void schedule_poll(uint32_t ms) {
  if (s_poll_timer) app_timer_cancel(s_poll_timer);
  s_poll_timer = s_visible ? app_timer_register(ms, poll, NULL) : NULL;
}

static void poll(void *ctx) {
  s_poll_timer = NULL;
  if (!s_visible) return;
  if (comm_is_busy()) { schedule_poll(1000); return; }
  comm_request(REQ_LIVE, s_service_type_id, s_plan_id, NULL, false);
}

static void format_clock(int32_t seconds, char *buf, size_t n) {
  if (seconds < 0) seconds = -seconds;
  int32_t h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
  if (h > 0) snprintf(buf, n, "%d:%02d:%02d", (int)h, (int)m, (int)s);
  else snprintf(buf, n, "%d:%02d", (int)m, (int)s);
}

// Draws text in a box and returns the height used.
static int draw_block(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align) {
  GSize size = graphics_text_layout_get_content_size(text, font, box, GTextOverflowModeTrailingEllipsis, align);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, align, NULL);
  return size.h;
}

static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_unobstructed_bounds(layer);
  // The big clock needs a tall screen to leave room for a two-line item name.
  bool large = ui_use_large_text(layer_get_bounds(window_get_root_layer(s_window)).size.h);
  int inset = PBL_IF_ROUND_ELSE(b.size.w / 9, 6);
  int x = b.origin.x + inset;
  int w = b.size.w - 2 * inset;
  int top = b.origin.y + PBL_IF_ROUND_ELSE(4, 2);
  int bottom = b.origin.y + b.size.h - PBL_IF_ROUND_ELSE(b.size.h / 8, 4);
  GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  LiveState *l = &g_data.live;

  GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont small_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  // The label under the clock and the "Next" line: bold black, larger on
  // Time 2 and Round 2. (Regular dark gray was hard to read on a Time 2.)
  GFont info_font = fonts_get_system_font(large ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
  int info_line = large ? 28 : 22;
  GFont clock_font = fonts_get_system_font(large ? FONT_KEY_LECO_42_NUMBERS : FONT_KEY_LECO_32_BOLD_NUMBERS);
  int clock_h = large ? 46 : 34;

  if (!l->has_data) {
    const char *msg = "Loading...";
    if (s_error == ERR_NO_PHONE) msg = "No phone connection";
    else if (s_error == ERR_NETWORK) msg = "No internet";
    else if (s_error == ERR_RATE_LIMIT) msg = "Busy. Retrying...";
    else if (s_error == ERR_NOT_FOUND) msg = "No live data for this plan";
    else if (s_error != ERR_NONE) msg = "Couldn't load. Retrying...";
    graphics_context_set_text_color(ctx, GColorBlack);
    draw_block(ctx, msg, title_font, GRect(x, b.origin.y + b.size.h / 2 - 34, w, 68), GTextAlignmentCenter);
    return;
  }

  int32_t elapsed = (int32_t)(time(NULL) - l->item_started);
  if (elapsed < 0) elapsed = 0;
  bool has_length = l->item_length > 0;
  int32_t remaining = l->item_length - elapsed;
  bool over = has_length && remaining < 0;
  bool stale = (time(NULL) - l->last_update) > STALE_SECONDS;

  // "Next" is anchored to the bottom: two lines on tall rectangles (Time 2)
  // with large text, else one, so the current item keeps its room.
  int next_h = 0;
  if (l->next[0]) {
    char next[TITLE_LEN + 8];
    snprintf(next, sizeof(next), "Next: %s", l->next);
    next_h = info_line * PBL_IF_ROUND_ELSE(1, (large && b.size.h >= 200) ? 2 : 1);
    graphics_context_set_text_color(ctx, GColorBlack);
    // The circle is narrower near the bottom of round screens.
    int nx = PBL_IF_ROUND_ELSE(b.size.w / 10, 0);
    draw_block(ctx, next, info_font, GRect(x + nx, bottom - next_h, w - 2 * nx, next_h), align);
  }

  if (!l->live) {
    graphics_context_set_text_color(ctx, ACCENT_COLOR);
    draw_block(ctx, "Not started", title_font, GRect(x, top + 4, w, 34), align);
    graphics_context_set_text_color(ctx, GColorBlack);
    draw_block(ctx, "The live view follows the service once it begins in Planning Center.",
               small_font, GRect(x, top + 40, w, bottom - next_h - top - 44), align);
    return;
  }

  // Fit title (28 pt, up to 2 lines), clock, label and bar above "Next".
  const int gap = 2;
  int label_h = large ? 26 : 20;
  int bar_h = has_length ? 8 : 0;
  int avail = bottom - top - next_h;
  int title_max = avail - (gap + clock_h + label_h + bar_h);
  if (title_max > 62) title_max = 62;
  // The item name matters more than the bar (the label already says how
  // much is left): drop the bar when the name needs the room.
  int title_need = graphics_text_layout_get_content_size(l->current, title_font,
      GRect(0, 0, w, 62), GTextOverflowModeTrailingEllipsis, align).h;
  if (title_need > title_max && bar_h) {
    title_max += bar_h;
    bar_h = 0;
    if (title_max > 62) title_max = 62;
  }
  // A few pixels short (Time Round): take them from the spacing.
  if (title_need > title_max && title_need - title_max <= 4) title_max = title_need;
  if (title_max < 30) title_max = 30;
  int title_h = graphics_text_layout_get_content_size(l->current, title_font,
      GRect(0, 0, w, title_max), GTextOverflowModeTrailingEllipsis, align).h;
  if (title_h > title_max) title_h = title_max;
  int block_h = title_h + gap + clock_h + label_h + bar_h;
  int y = top + (avail > block_h ? (avail - block_h) / 2 : 0);

  graphics_context_set_text_color(ctx, ACCENT_COLOR);
  draw_block(ctx, l->current, title_font, GRect(x, y - 4, w, title_max), align);
  y += title_h + gap;

  // Clock: time left in the item, or time over it with a minus sign. Over
  // time is red on color watches; black and white watches show white
  // digits on a black band instead.
  char clock[17];
  clock[0] = '-';
  format_clock(has_length ? remaining : elapsed, clock + 1, sizeof(clock) - 1);
  const char *clock_text = over ? clock : clock + 1;
  GColor clock_color = over ? ALERT_COLOR : GColorBlack;
  int clock_x = x;
#if defined(PBL_BW)
  if (over) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(x - 2, y + CLOCK_BAND_TOP, w + 4, clock_h - CLOCK_BAND_TOP + 1), 3, GCornersAll);
    clock_color = GColorWhite;
    clock_x = x + 3;  // room inside the band's left edge
  }
#endif
  graphics_context_set_text_color(ctx, clock_color);
  graphics_draw_text(ctx, clock_text, clock_font, GRect(clock_x, y, w - (clock_x - x), clock_h),
                     GTextOverflowModeTrailingEllipsis, align, NULL);
  y += clock_h;

  // Label under the clock.
  char label[40];
  char len_buf[16];
  format_clock(l->item_length, len_buf, sizeof(len_buf));
  if (stale) snprintf(label, sizeof(label), "Reconnecting...");
  else if (!has_length) snprintf(label, sizeof(label), "elapsed");
  else if (over) snprintf(label, sizeof(label), "over %s", len_buf);
  else snprintf(label, sizeof(label), "left of %s", len_buf);
  graphics_context_set_text_color(ctx, over && !stale ? ALERT_COLOR : GColorBlack);
  graphics_draw_text(ctx, label, info_font, GRect(x, y - 4, w, info_line),
                     GTextOverflowModeTrailingEllipsis, align, NULL);
  y += label_h;

  // Progress bar.
  if (bar_h) {
    int bar_w = w;
    int filled = over ? bar_w : (int)(bar_w * elapsed / l->item_length);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_rect(ctx, GRect(x, y, bar_w, 6));
    graphics_context_set_fill_color(ctx, over ? ALERT_COLOR : ACCENT_COLOR);
    graphics_fill_rect(ctx, GRect(x, y, filled, 6), 0, GCornerNone);
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void comm_handler(RequestType type, CommEvent event, CommError code) {
  if (type != REQ_LIVE) return;
  if (event == COMM_DONE) {
    s_error = ERR_NONE;
    s_backoff_ms = 0;
    schedule_poll(POLL_MS);
  } else if (event == COMM_ERROR) {
    s_error = code;
    // Back off: 5 s, 10 s, 20 s ... up to 60 s, and at least Retry-After.
    s_backoff_ms = s_backoff_ms ? s_backoff_ms * 2 : POLL_MS * 2;
    if (s_backoff_ms > MAX_BACKOFF_MS) s_backoff_ms = MAX_BACKOFF_MS;
    uint32_t wait = s_backoff_ms;
    if (code == ERR_RATE_LIMIT && (uint32_t)comm_retry_after() * 1000 > wait) {
      wait = comm_retry_after() * 1000;
    }
    if (code != ERR_AUTH) schedule_poll(wait);
  }
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// Remembers the item before a poll so a change can vibrate.
static char s_prev_item[TITLE_LEN];

static void check_item_change(void) {
  LiveState *l = &g_data.live;
  if (l->has_data && l->live && s_prev_item[0] && strcmp(s_prev_item, l->current) != 0 &&
      g_data.vibe_on_change) {
    vibes_short_pulse();
  }
  data_copy(s_prev_item, l->live ? l->current : "", TITLE_LEN);
}

static void comm_handler_with_vibe(RequestType type, CommEvent event, CommError code) {
  comm_handler(type, event, code);
  if (type == REQ_LIVE && event == COMM_DONE) check_item_change();
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_colors(s_status_bar, ACCENT_COLOR, GColorWhite);
  status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeNone);
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  s_canvas = layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w,
                                bounds.size.h - STATUS_BAR_LAYER_HEIGHT));
  layer_set_update_proc(s_canvas, draw);
  layer_add_child(root, s_canvas);
}

static void window_appear(Window *window) {
  s_visible = true;
  comm_set_handler(comm_handler_with_vibe);
  // SECOND_UNIT only while this screen is open.
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  schedule_poll(10);
}

static void window_disappear(Window *window) {
  s_visible = false;
  tick_timer_service_unsubscribe();
  if (s_poll_timer) { app_timer_cancel(s_poll_timer); s_poll_timer = NULL; }
  comm_cancel();
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
  status_bar_layer_destroy(s_status_bar);
  s_status_bar = NULL;
  window_destroy(window);
  s_window = NULL;
}

void live_window_push(const char *service_type_id, const char *plan_id) {
  data_copy(s_service_type_id, service_type_id, ID_LEN);
  data_copy(s_plan_id, plan_id, ID_LEN);
  memset(&g_data.live, 0, sizeof(g_data.live));
  s_prev_item[0] = '\0';
  s_error = ERR_NONE;
  s_backoff_ms = 0;

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .appear = window_appear,
    .disappear = window_disappear,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
