#include "ui.h"
#include "comm.h"

// Plan order. Row 0 opens the live view; the rest are the plan's items.

static Window *s_window;
static MenuLayer *s_menu;
static PlanRow s_plan;
static bool s_loading;
static CommError s_error;

// Marquee for the selected item: text too wide for the row waits, scrolls
// to its end, waits, and jumps back. A few rounds, then it rests, so the
// screen does not redraw forever. Off on aplite: its 24 kB for code and heap
// cannot spare the code size, and long text keeps its "..." there.
#if defined(PBL_PLATFORM_APLITE)
#define MARQUEE 0
#else
#define MARQUEE 1
#endif

#define MARQUEE_START_MS 1000
#define MARQUEE_END_MS 1200
#define MARQUEE_FRAME_MS 50
#define MARQUEE_STEP_PX 3
#define MARQUEE_ROUNDS 3
#define CELL_INSET PBL_IF_ROUND_ELSE(12, 5)

static AppTimer *s_marquee_timer;
static int16_t s_marquee_offset;  // pixels scrolled
static int16_t s_marquee_max;     // widest overflow of the selected row
static uint8_t s_marquee_rounds;

static bool items_ready(void) {
  return g_data.items_loaded && strcmp(g_data.items_plan_id, s_plan.plan_id) == 0;
}

static uint16_t get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  if (!items_ready() || g_data.item_count == 0) return 2;  // Live + status row
  return 1 + g_data.item_count;
}

static bool is_header_row(uint16_t row) {
  return row > 0 && items_ready() && row - 1 < g_data.item_count &&
         g_data.items[row - 1].kind == KIND_HEADER;
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (is_header_row(idx->row)) return 30;
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

#if MARQUEE
// Same fonts as menu_cell_basic_draw, so a row does not change when it scrolls.
static GFont title_font(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD); }
static GFont subtitle_font(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_18); }

static int text_width(const char *text, GFont font) {
  if (!text || !text[0]) return 0;
  return graphics_text_layout_get_content_size(text, font, GRect(0, 0, 2000, 40),
      GTextOverflowModeFill, GTextAlignmentLeft).w;
}

// How far the widest line of a row must scroll to show its end. 0 if it fits.
static int row_overflow(const ItemRow *it, int cell_w) {
  int avail = cell_w - 2 * CELL_INSET;
  int t = text_width(it->title, title_font()) - avail;
  int s = text_width(it->subtitle, subtitle_font()) - avail;
  int m = t > s ? t : s;
  return m > 0 ? m + 4 : 0;
}

// One line of the selected row, shifted left by the marquee offset (each
// line stops at its own end). The cell layer clips the text.
static void draw_scrolling_line(GContext *ctx, const char *text, GFont font, int y, int h, int cell_w) {
  int avail = cell_w - 2 * CELL_INSET;
  int w = text_width(text, font);
  int over = w - avail + 4;
  int off = over > 0 ? (s_marquee_offset < over ? s_marquee_offset : over) : 0;
  GTextAlignment align = over > 0 ? GTextAlignmentLeft : PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  GRect box = over > 0 ? GRect(CELL_INSET - off, y, w + 8, h) : GRect(CELL_INSET, y, avail, h);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void draw_selected_item(GContext *ctx, const Layer *cell, const ItemRow *it) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_text_color(ctx, GColorWhite);
  bool sub = it->subtitle[0] != '\0';
  // Matches menu_cell_basic_draw: title and subtitle centered as a block.
  int block = sub ? 24 + 20 : 28;
  int y = (b.size.h - block) / 2 - 6;
  draw_scrolling_line(ctx, it->title, title_font(), y + PBL_IF_ROUND_ELSE(2, -1), 30, b.size.w);
  if (sub) draw_scrolling_line(ctx, it->subtitle, subtitle_font(), y + PBL_IF_ROUND_ELSE(28, 26), 24, b.size.w);
}
#endif

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
#if MARQUEE
  } else if (menu_cell_layer_is_highlighted(cell) && s_marquee_max > 0) {
    draw_selected_item(ctx, cell, it);
#endif
  } else {
    menu_cell_basic_draw(ctx, cell, it->title, it->subtitle[0] ? it->subtitle : NULL, NULL);
  }
}

#if MARQUEE
static void marquee_step(void *ctx);

static void marquee_schedule(uint32_t ms) {
  s_marquee_timer = app_timer_register(ms, marquee_step, NULL);
}

static void marquee_stop(void) {
  if (s_marquee_timer) { app_timer_cancel(s_marquee_timer); s_marquee_timer = NULL; }
}

static void marquee_step(void *ctx) {
  s_marquee_timer = NULL;
  if (!s_menu) return;
  if (s_marquee_offset >= s_marquee_max) {
    // At the end: jump back to the start, then maybe go again.
    s_marquee_offset = 0;
    s_marquee_rounds++;
    if (s_marquee_rounds < MARQUEE_ROUNDS) marquee_schedule(MARQUEE_START_MS + MARQUEE_END_MS);
  } else {
    s_marquee_offset += MARQUEE_STEP_PX;
    if (s_marquee_offset >= s_marquee_max) {
      s_marquee_offset = s_marquee_max;
      marquee_schedule(MARQUEE_END_MS);
    } else {
      marquee_schedule(MARQUEE_FRAME_MS);
    }
  }
  layer_mark_dirty(menu_layer_get_layer(s_menu));
}

// Starts the marquee for the selected row if its text does not fit.
static void marquee_reset(void) {
  marquee_stop();
  s_marquee_offset = 0;
  s_marquee_rounds = 0;
  s_marquee_max = 0;
  if (!s_menu || !items_ready()) return;
  int row = menu_layer_get_selected_index(s_menu).row;
  if (row < 1 || row - 1 >= g_data.item_count || is_header_row(row)) return;
  int w = layer_get_bounds(menu_layer_get_layer(s_menu)).size.w;
  s_marquee_max = row_overflow(&g_data.items[row - 1], w);
  if (s_marquee_max > 0) marquee_schedule(MARQUEE_START_MS);
}

static void selection_changed(MenuLayer *ml, MenuIndex new_index, MenuIndex old_index, void *ctx) {
  marquee_reset();
}
#else
static void marquee_reset(void) {}
static void marquee_stop(void) {}
#endif

// Section headers are dividers, not choices: the cursor skips over them in
// the direction it was moving, and stays put if only headers are left.
// A jump of more than one row is a tap on the header (Time 2, Round 2):
// keep the selection where it was.
static void selection_will_change(MenuLayer *ml, MenuIndex *new_index, MenuIndex old_index, void *ctx) {
  if (!is_header_row(new_index->row)) return;
  int jump = new_index->row - old_index.row;
  if (jump > 1 || jump < -1) { *new_index = old_index; return; }
  int step = jump > 0 ? 1 : -1;
  int rows = get_num_rows(ml, 0, NULL);
  for (int r = new_index->row + step; r >= 0 && r < rows; r += step) {
    if (!is_header_row(r)) { new_index->row = r; return; }
  }
  *new_index = old_index;
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
  if (event == COMM_DONE) marquee_reset();
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
    .selection_will_change = selection_will_change,
#if MARQUEE
    .selection_changed = selection_changed,
#endif
  });
  menu_layer_set_highlight_colors(s_menu, ACCENT_COLOR, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  if (!items_ready()) load_items();
}

static void window_appear(Window *window) {
  comm_set_handler(comm_handler);
  if (s_loading && comm_current() != REQ_ITEMS) load_items();
  marquee_reset();
}

static void window_disappear(Window *window) {
  marquee_stop();
  s_marquee_offset = 0;
}

static void window_unload(Window *window) {
  marquee_stop();
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
    .disappear = window_disappear,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
