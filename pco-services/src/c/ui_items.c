#include "ui.h"
#include "comm.h"

// Plan order. Row 0 opens the live view; the rest are the plan's items.

static Window *s_window;
static MenuLayer *s_menu;
static PlanRow s_plan;
static bool s_loading;
static CommError s_error;

static bool items_ready(void) {
  return g_data.items_loaded && strcmp(g_data.items_plan_id, s_plan.plan_id) == 0;
}

static uint16_t get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  if (!items_ready() || g_data.item_count == 0) return 2;  // Live + status row
  return 1 + g_data.item_count;
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (idx->row > 0 && items_ready() && idx->row - 1 < g_data.item_count &&
      g_data.items[idx->row - 1].kind == KIND_HEADER) {
    return PBL_IF_ROUND_ELSE(menu_layer_is_index_selected(ml, idx) ? 44 : 30, 30);
  }
  return PBL_IF_ROUND_ELSE(menu_layer_is_index_selected(ml, idx) ? 60 : 44, 44);
}

static int16_t get_header_height(MenuLayer *ml, uint16_t section, void *ctx) {
  return UI_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
  char buf[TITLE_LEN + SUBTITLE_LEN + 8];
  snprintf(buf, sizeof(buf), "%s%s%s", s_plan.title, s_plan.subtitle[0] ? " · " : "", s_plan.subtitle);
  ui_draw_header(ctx, cell, buf);
}

// Headers in the plan order are drawn as bold dividers.
static void draw_divider(GContext *ctx, const Layer *cell, const char *text, bool selected) {
  GRect b = layer_get_bounds(cell);
  if (!selected) {
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite));
    graphics_fill_rect(ctx, b, 0, GCornerNone);
  }
  graphics_context_set_text_color(ctx, selected ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(TEXT_INSET, (b.size.h - 22) / 2 - 2, b.size.w - 2 * TEXT_INSET, 22),
                     GTextOverflowModeTrailingEllipsis,
                     PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);
  PBL_IF_BW_ELSE(graphics_draw_line(ctx, GPoint(0, b.size.h - 1), GPoint(b.size.w, b.size.h - 1)), (void)0);
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  if (idx->row == 0) {
    menu_cell_basic_draw(ctx, cell, "Live view", "Follow the service", NULL);
    return;
  }
  if (!items_ready() || g_data.item_count == 0) {
    const char *title = "Loading...";
    const char *sub = NULL;
    if (!s_loading) {
      if (s_error == ERR_NO_PHONE) { title = "No phone"; sub = "Select to retry"; }
      else if (s_error == ERR_NETWORK) { title = "Offline"; sub = "Select to retry"; }
      else if (s_error == ERR_NOT_FOUND) { title = "Plan not found"; }
      else if (s_error != ERR_NONE) { title = "Couldn't load"; sub = "Select to retry"; }
      else if (items_ready()) { title = "No items"; sub = "This plan is empty"; }
    }
    menu_cell_basic_draw(ctx, cell, title, sub, NULL);
    return;
  }
  ItemRow *it = &g_data.items[idx->row - 1];
  if (it->kind == KIND_HEADER) {
    draw_divider(ctx, cell, it->title, menu_cell_layer_is_highlighted(cell));
  } else {
    menu_cell_basic_draw(ctx, cell, it->title, it->subtitle[0] ? it->subtitle : NULL, NULL);
  }
}

static void load_items(void) {
  s_loading = true;
  s_error = ERR_NONE;
  if (s_menu) menu_layer_reload_data(s_menu);
  comm_request(REQ_ITEMS, s_plan.service_type_id, s_plan.plan_id, NULL, false);
}

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (idx->row == 0) {
    live_window_push(s_plan.service_type_id, s_plan.plan_id);
  } else if (!s_loading && (!items_ready() || g_data.item_count == 0)) {
    load_items();
  }
}

static void comm_handler(RequestType type, CommEvent event, CommError code) {
  if (type != REQ_ITEMS) return;
  if (event != COMM_PROGRESS) {
    s_loading = false;
    s_error = event == COMM_ERROR ? code : ERR_NONE;
    if (event == COMM_ERROR && code == ERR_NETWORK) {
      message_show(MSG_NO_INTERNET, "No internet", "Your phone can't reach Planning Center right now.", 0);
    } else if (event == COMM_ERROR && code == ERR_NO_PHONE) {
      message_show(MSG_NO_PHONE, "No phone", "Your watch is not connected to your phone.", 0);
    }
  }
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void window_load(Window *window) {
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
  if (!items_ready()) load_items();
}

static void window_appear(Window *window) {
  comm_set_handler(comm_handler);
  if (s_loading && comm_current() != REQ_ITEMS) load_items();
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(window);
  s_window = NULL;
}

void items_window_push(const PlanRow *plan) {
  s_plan = *plan;
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .appear = window_appear,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
