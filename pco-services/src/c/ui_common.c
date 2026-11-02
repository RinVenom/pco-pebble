#include "ui.h"

// Menu section header: 18 pt bold, centered on round screens so the circle
// does not clip it.
void ui_draw_header(GContext *ctx, const Layer *cell, const char *text) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, ACCENT_COLOR);
  int inset = PBL_IF_ROUND_ELSE(b.size.w / 8, TEXT_INSET);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(inset, -3, b.size.w - 2 * inset, b.size.h + 3),
                     GTextOverflowModeTrailingEllipsis,
                     PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);
}

bool ui_use_large_text(int screen_height) {
#if PBL_DISPLAY_HEIGHT < 200
  return false;  // small screens: compiled out
#else
  if (screen_height < 200) return false;
  // On Time 2 and Round 2 the default Text Size (Medium) reports Large.
  return preferred_content_size() >= PreferredContentSizeLarge;
#endif
}
