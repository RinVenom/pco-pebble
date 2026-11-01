#include "ui.h"
#include "comm.h"

// Sections: [status] [one per service type ...] [More]
#define MAX_GROUPS MAX_PLANS

static Window *s_window;
static MenuLayer *s_menu;

static int s_group_start[MAX_GROUPS];
static int s_group_count[MAX_GROUPS];
static int s_group_total;

static bool s_loading;
static bool s_waiting_for_phone;
static CommError s_error;
static char s_status[48];
static bool s_pending_refresh;

static void rebuild_groups(void) {
  s_group_total = 0;
  for (int i = 0; i < g_data.plan_count; i++) {
    if (s_group_total == 0 ||
        strcmp(g_data.plans[i].section, g_data.plans[s_group_start[s_group_total - 1]].section) != 0) {
      if (s_group_total >= MAX_GROUPS) break;
      s_group_start[s_group_total] = i;
      s_group_count[s_group_total] = 0;
      s_group_total++;
    }
    s_group_count[s_group_total - 1]++;
  }
}

static void update_status(void) {
  char age[16];
  data_format_age(g_data.plans_updated, age, sizeof(age));
  if (s_waiting_for_phone && g_data.plan_count == 0) {
    snprintf(s_status, sizeof(s_status), "Connecting to phone...");
  } else if (s_loading) {
    snprintf(s_status, sizeof(s_status), g_data.plan_count ? "Refreshing..." : "Loading plans...");
  } else if (s_error == ERR_NO_PHONE) {
    snprintf(s_status, sizeof(s_status), "No phone · %s", age);
  } else if (s_error == ERR_NETWORK) {
    snprintf(s_status, sizeof(s_status), "Offline · %s", age);
  } else if (s_error == ERR_RATE_LIMIT) {
    snprintf(s_status, sizeof(s_status), "Busy, try soon · %s", age);
  } else if (s_error != ERR_NONE) {
    snprintf(s_status, sizeof(s_status), "Couldn't load · %s", age);
  } else if (g_data.plans_updated) {
    snprintf(s_status, sizeof(s_status), "Updated %s", age);
  } else {
    s_status[0] = '\0';
  }
}

static void reload(void) {
  rebuild_groups();
  update_status();
  if (s_menu) menu_layer_reload_data(s_menu);
}

// Section helpers.
static bool has_empty_row(void) {
  return !s_loading && !s_waiting_for_phone && g_data.plans_updated && g_data.plan_count == 0;
}
static int first_group_section(void) { return 1; }
static int more_section(void) { return 1 + s_group_total + (has_empty_row() ? 1 : 0); }

static uint16_t get_num_sections(MenuLayer *ml, void *ctx) {
  return more_section() + 1;
}

static uint16_t get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  if (section == 0) return 1;                      // status row
  if (section == more_section()) return 1;         // Requests
  int g = section - first_group_section();
  if (g < s_group_total) return s_group_count[g];
  return 1;                                        // "No upcoming plans"
}

static int16_t get_header_height(MenuLayer *ml, uint16_t section, void *ctx) {
  if (section == 0) return 0;
  return UI_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
  int g = section - first_group_section();
  const char *text = "More";
  if (section != (uint16_t)more_section()) {
    text = g < s_group_total ? g_data.plans[s_group_start[g]].section : "Plans";
  }
  ui_draw_header(ctx, cell, text);
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (idx->section == 0) return 30;
  return PBL_IF_ROUND_ELSE(menu_layer_is_index_selected(ml, idx) ? 60 : 44, 44);
}

static void draw_small_row(GContext *ctx, const Layer *cell, const char *text, bool selected) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_text_color(ctx, selected ? GColorWhite : MUTED_COLOR);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(TEXT_INSET, (b.size.h - 22) / 2 - 2, b.size.w - 2 * TEXT_INSET, 22),
                     GTextOverflowModeTrailingEllipsis,
                     PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  bool selected = menu_cell_layer_is_highlighted(cell);
  if (idx->section == 0) {
    draw_small_row(ctx, cell, s_status[0] ? s_status : "Refresh", selected);
    return;
  }
  if (idx->section == more_section()) {
    menu_cell_basic_draw(ctx, cell, "Requests", "Accept or decline", NULL);
    return;
  }
  int g = idx->section - first_group_section();
  if (g >= s_group_total) {
    menu_cell_basic_draw(ctx, cell, "No plans", "Nothing upcoming", NULL);
    return;
  }
  PlanRow *p = &g_data.plans[s_group_start[g] + idx->row];
  menu_cell_basic_draw(ctx, cell, p->title, p->subtitle[0] ? p->subtitle : NULL, NULL);
}

static PlanRow *plan_at(MenuIndex *idx) {
  int g = idx->section - first_group_section();
  if (idx->section == 0 || idx->section == more_section() || g >= s_group_total) return NULL;
  return &g_data.plans[s_group_start[g] + idx->row];
}

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (idx->section == 0) { plans_window_refresh(); return; }
  if (idx->section == more_section()) { requests_window_push(); return; }
  PlanRow *p = plan_at(idx);
  if (!p) return;
  data_copy(g_data.last_service_type, p->service_type_id, ID_LEN);
  items_window_push(p);
}

// Long press on a plan jumps straight to its live view.
static void select_long_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  PlanRow *p = plan_at(idx);
  if (!p) return;
  data_copy(g_data.last_service_type, p->service_type_id, ID_LEN);
  live_window_push(p->service_type_id, p->plan_id);
}

static void comm_handler(RequestType type, CommEvent event, CommError code) {
  if (type != REQ_PLANS) return;
  if (event == COMM_PROGRESS) { reload(); return; }
  s_loading = false;
  s_error = event == COMM_ERROR ? code : ERR_NONE;
  reload();
  if (event == COMM_DONE && g_data.plan_count > 0 && s_menu &&
      menu_layer_get_selected_index(s_menu).section == 0) {
    menu_layer_set_selected_index(s_menu, MenuIndex(1, 0), MenuRowAlignCenter, false);
  }
  if (event == COMM_ERROR) {
    if (code == ERR_NO_PHONE) {
      message_show(MSG_NO_PHONE, "No phone", "Your watch is not connected to your phone. Plans shown are from your last update.", 0);
    } else if (code == ERR_NETWORK) {
      message_show(MSG_NO_INTERNET, "No internet", "Your phone can't reach Planning Center right now.", 0);
    }
  }
}

void plans_window_refresh(void) {
  if (window_stack_get_top_window() != s_window) {
    // Do not interrupt another screen's request (such as live polling).
    s_pending_refresh = true;
    return;
  }
  s_pending_refresh = false;
  s_loading = true;
  s_error = ERR_NONE;
  reload();
  comm_request(REQ_PLANS, NULL, NULL, NULL, false);
}

void plans_window_set_waiting(bool waiting) {
  s_waiting_for_phone = waiting;
  reload();
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_sections = get_num_sections,
    .get_num_rows = get_num_rows,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
    .select_long_click = select_long_click,
  });
  menu_layer_set_highlight_colors(s_menu, ACCENT_COLOR, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  reload();
  // Start on the first plan, not the status row.
  if (g_data.plan_count > 0) {
    menu_layer_set_selected_index(s_menu, MenuIndex(1, 0), MenuRowAlignCenter, false);
  }
}

static void window_appear(Window *window) {
  comm_set_handler(comm_handler);
  // Refresh if one was put off, or if another screen replaced our request.
  if (s_pending_refresh || (s_loading && comm_current() != REQ_PLANS)) {
    plans_window_refresh();
  } else {
    reload();
  }
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
}

void plans_window_push(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .appear = window_appear,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

Window *plans_window_get(void) { return s_window; }
