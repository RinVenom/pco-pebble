#include "ui.h"

static Window *s_window;
static Layer *s_canvas;
static MessageKind s_kind;
static char s_title[32];
static char s_body[160];
static AppTimer *s_close_timer;

static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_unobstructed_bounds(layer);
  int inset = PBL_IF_ROUND_ELSE(b.size.w / 8, 8);
  GRect box = grect_inset(b, GEdgeInsets(0, inset));

  GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont body_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  GTextAlignment align = GTextAlignmentCenter;

  GSize ts = graphics_text_layout_get_content_size(s_title, title_font,
      GRect(0, 0, box.size.w, 60), GTextOverflowModeWordWrap, align);
  GSize bs = graphics_text_layout_get_content_size(s_body, body_font,
      GRect(0, 0, box.size.w, b.size.h), GTextOverflowModeWordWrap, align);
  int gap = 6;
  int total = ts.h + gap + bs.h;
  int y = box.origin.y + (box.size.h - total) / 2;
  if (y < box.origin.y + 4) y = box.origin.y + 4;

  graphics_context_set_text_color(ctx, s_kind == MSG_SIGN_IN ? ACCENT_COLOR : GColorBlack);
  graphics_draw_text(ctx, s_title, title_font, GRect(box.origin.x, y, box.size.w, ts.h + 4),
                     GTextOverflowModeWordWrap, align, NULL);
  y += ts.h + gap;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_body, body_font, GRect(box.origin.x, y, box.size.w, b.size.h - y),
                     GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void back_click(ClickRecognizerRef rec, void *ctx) {
  if (s_kind == MSG_SIGN_IN) {
    // Nothing works until sign-in, so BACK leaves the app.
    window_stack_pop_all(true);
  } else {
    window_stack_remove(s_window, true);
  }
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  Layer *canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(canvas, draw);
  layer_add_child(root, canvas);
  window_set_user_data(window, canvas);
  s_canvas = canvas;
}

static void window_unload(Window *window) {
  Layer *canvas = window_get_user_data(window);
  if (window == s_window) {
    if (s_close_timer) { app_timer_cancel(s_close_timer); s_close_timer = NULL; }
    s_window = NULL;
    s_canvas = NULL;
  }
  layer_destroy(canvas);
  window_destroy(window);
}

static void close_timer_cb(void *ctx) {
  s_close_timer = NULL;
  if (s_window) window_stack_remove(s_window, true);
}

void message_show(MessageKind kind, const char *title, const char *body, uint32_t auto_close_ms) {
  s_kind = kind;
  data_copy(s_title, title, sizeof(s_title));
  data_copy(s_body, body, sizeof(s_body));
  if (s_close_timer) { app_timer_cancel(s_close_timer); s_close_timer = NULL; }

  if (!s_window) {
    s_window = window_create();
    window_set_background_color(s_window, GColorWhite);
    window_set_click_config_provider(s_window, click_config);
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
    window_stack_push(s_window, true);
  } else {
    if (window_stack_get_top_window() != s_window) {
      // Something was pushed above it: drop this one and show a new one on top.
      Window *old = s_window;
      s_window = NULL;
      s_canvas = NULL;
      window_stack_remove(old, false);
      message_show(kind, title, body, auto_close_ms);
      return;
    }
    if (s_canvas) layer_mark_dirty(s_canvas);
  }
  if (auto_close_ms) s_close_timer = app_timer_register(auto_close_ms, close_timer_cb, NULL);
}

void message_hide(MessageKind kind) {
  if (s_window && s_kind == kind) window_stack_remove(s_window, true);
}

bool message_is_showing(MessageKind kind) {
  return s_window && s_kind == kind;
}
