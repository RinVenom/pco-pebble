#include "ui.h"
#include "comm.h"

// Pending scheduling requests, and a detail screen with an ActionBarLayer:
// UP accepts, DOWN declines.

static Window *s_list_window;
static MenuLayer *s_menu;
static bool s_loading;
static CommError s_error;

static Window *s_detail_window;
static ActionBarLayer *s_action_bar;
static Layer *s_detail_canvas;
static GBitmap *s_icon_accept;
static GBitmap *s_icon_decline;
static RequestRow s_selected;
static bool s_sending;
static bool s_sent_accept;

// ---- Detail ---------------------------------------------------------------

// All text is black and bold for contrast. Large screens (Time 2, Round 2)
// get bigger fonts; the button hint sits at the bottom.
static void detail_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_unobstructed_bounds(layer);
  bool large = b.size.h >= 200;  // Time 2, Round 2
  int inset = PBL_IF_ROUND_ELSE(b.size.w / 7, 6);
  int x = b.origin.x + inset;
  int w = b.size.w - 2 * inset + PBL_IF_ROUND_ELSE(0, 2);
  int top = b.origin.y + PBL_IF_ROUND_ELSE(b.size.h / 6, 2);
  int bottom = b.origin.y + b.size.h - PBL_IF_ROUND_ELSE(b.size.h / 6, 4);
  GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentRight, GTextAlignmentLeft);

  GFont title_font = fonts_get_system_font(large ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD);
  GFont body_font = fonts_get_system_font(large ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
  GFont hint_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  graphics_context_set_text_color(ctx, GColorBlack);

  // Hint, anchored to the bottom. Round screens have no room for it; the
  // action bar icons say the same thing.
  const char *hint = s_sending ? "Sending..." : PBL_IF_ROUND_ELSE("", "Up: accept\nDown: decline");
  int hint_h = graphics_text_layout_get_content_size(hint, hint_font, GRect(0, 0, w, 44),
      GTextOverflowModeWordWrap, align).h;
  graphics_draw_text(ctx, hint, hint_font, GRect(x, bottom - hint_h - 4, w, hint_h + 4),
                     GTextOverflowModeWordWrap, align, NULL);
  int text_bottom = hint[0] ? bottom - hint_h - 6 : bottom;

  // Title (team or service), then date and position.
  int y = top;
  int max_title = large ? 64 : 54;
  int title_h = graphics_text_layout_get_content_size(s_selected.title, title_font,
      GRect(0, 0, w, max_title), GTextOverflowModeTrailingEllipsis, align).h;
  graphics_draw_text(ctx, s_selected.title, title_font, GRect(x, y - 4, w, max_title),
                     GTextOverflowModeTrailingEllipsis, align, NULL);
  y += (title_h < max_title ? title_h : max_title) + 2;

  // "Nov 8 · Acoustic Guitar" -> date and position on their own lines.
  char sub[SUBTITLE_LEN + 2];
  data_copy(sub, s_selected.subtitle, sizeof(sub));
  char *dot = strstr(sub, " \xC2\xB7 ");
  if (dot) {
    *dot = '\n';
    memmove(dot + 1, dot + 4, strlen(dot + 4) + 1);
  }
  if (text_bottom > y) {
    graphics_draw_text(ctx, sub, body_font, GRect(x, y - 2, w, text_bottom - y),
                       GTextOverflowModeTrailingEllipsis, align, NULL);
  }
}

static void send_response(bool accept) {
  if (s_sending) return;
  s_sending = true;
  s_sent_accept = accept;
  layer_mark_dirty(s_detail_canvas);
  comm_request(REQ_RESPOND, NULL, NULL, s_selected.id, accept);
}

static void accept_click(ClickRecognizerRef rec, void *ctx) { send_response(true); }
static void decline_click(ClickRecognizerRef rec, void *ctx) { send_response(false); }

static void detail_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, accept_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, decline_click);
}

static void remove_request(const char *id) {
  for (int i = 0; i < g_data.request_count; i++) {
    if (strcmp(g_data.requests[i].id, id) == 0) {
      for (int j = i; j < g_data.request_count - 1; j++) g_data.requests[j] = g_data.requests[j + 1];
      g_data.request_count--;
      return;
    }
  }
}

static void detail_comm_handler(RequestType type, CommEvent event, CommError code) {
  if (type != REQ_RESPOND) return;
  s_sending = false;
  if (event == COMM_DONE) {
    remove_request(s_selected.id);
    window_stack_remove(s_detail_window, false);
    message_show(MSG_RESULT, s_sent_accept ? "Accepted" : "Declined",
                 s_sent_accept ? "Thanks for serving!" : "Your scheduler will be told.", 2000);
  } else {
    const char *why = code == ERR_NETWORK ? "No internet connection." :
                      code == ERR_NO_PHONE ? "Your phone is not connected." :
                      "Planning Center did not accept the response. Try again.";
    message_show(MSG_RESULT, "Not sent", why, 3000);
  }
  if (s_detail_canvas) layer_mark_dirty(s_detail_canvas);
}

static void detail_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_icon_accept = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ACCEPT);
  s_icon_decline = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DECLINE);

  s_detail_canvas = layer_create(GRect(0, 0, bounds.size.w - ACTION_BAR_WIDTH, bounds.size.h));
  layer_set_update_proc(s_detail_canvas, detail_draw);
  layer_add_child(root, s_detail_canvas);

  s_action_bar = action_bar_layer_create();
  action_bar_layer_set_background_color(s_action_bar, ACCENT_COLOR);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, s_icon_accept);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN, s_icon_decline);
  action_bar_layer_set_click_config_provider(s_action_bar, detail_click_config);
  action_bar_layer_add_to_window(s_action_bar, window);
}

static void detail_appear(Window *window) {
  comm_set_handler(detail_comm_handler);
}

static void detail_unload(Window *window) {
  action_bar_layer_destroy(s_action_bar);
  layer_destroy(s_detail_canvas);
  gbitmap_destroy(s_icon_accept);
  gbitmap_destroy(s_icon_decline);
  s_action_bar = NULL;
  s_detail_canvas = NULL;
  window_destroy(window);
  s_detail_window = NULL;
  if (s_sending) { comm_cancel(); s_sending = false; }
}

static void detail_push(const RequestRow *row) {
  s_selected = *row;
  s_sending = false;
  s_detail_window = window_create();
  window_set_window_handlers(s_detail_window, (WindowHandlers) {
    .load = detail_load,
    .appear = detail_appear,
    .unload = detail_unload,
  });
  window_stack_push(s_detail_window, true);
}

// ---- List -----------------------------------------------------------------

static uint16_t get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  if (s_loading || !g_data.requests_loaded || g_data.request_count == 0) return 1;
  return g_data.request_count;
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  return PBL_IF_ROUND_ELSE(menu_layer_is_index_selected(ml, idx) ? 60 : 44, 44);
}

static int16_t get_header_height(MenuLayer *ml, uint16_t section, void *ctx) {
  return UI_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
  ui_draw_header(ctx, cell, "Requests");
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  if (s_loading || !g_data.requests_loaded || g_data.request_count == 0) {
    const char *title = "Loading...";
    const char *sub = NULL;
    if (!s_loading) {
      if (s_error == ERR_NETWORK) { title = "Offline"; sub = "Select to retry"; }
      else if (s_error == ERR_NO_PHONE) { title = "No phone"; sub = "Select to retry"; }
      else if (s_error != ERR_NONE) { title = "Couldn't load"; sub = "Select to retry"; }
      else { title = "All caught up"; sub = "No pending requests"; }
    }
    menu_cell_basic_draw(ctx, cell, title, sub, NULL);
    return;
  }
  RequestRow *r = &g_data.requests[idx->row];
  menu_cell_basic_draw(ctx, cell, r->title, r->subtitle, NULL);
}

static void load_requests(void) {
  s_loading = true;
  s_error = ERR_NONE;
  if (s_menu) menu_layer_reload_data(s_menu);
  comm_request(REQ_REQUESTS, NULL, NULL, NULL, false);
}

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (s_loading) return;
  if (!g_data.requests_loaded || g_data.request_count == 0) { load_requests(); return; }
  if (idx->row < g_data.request_count) detail_push(&g_data.requests[idx->row]);
}

static void list_comm_handler(RequestType type, CommEvent event, CommError code) {
  if (type != REQ_REQUESTS) return;
  if (event != COMM_PROGRESS) {
    s_loading = false;
    s_error = event == COMM_ERROR ? code : ERR_NONE;
  }
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void list_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .draw_row = draw_row,
    .select_click = select_click,
  });
  menu_layer_set_highlight_colors(s_menu, ACCENT_COLOR, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  load_requests();
}

static void list_appear(Window *window) {
  comm_set_handler(list_comm_handler);
  if (s_loading && comm_current() != REQ_REQUESTS) load_requests();
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void list_unload(Window *window) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(window);
  s_list_window = NULL;
}

void requests_window_push(void) {
  s_list_window = window_create();
  window_set_window_handlers(s_list_window, (WindowHandlers) {
    .load = list_load,
    .appear = list_appear,
    .unload = list_unload,
  });
  window_stack_push(s_list_window, true);
}
