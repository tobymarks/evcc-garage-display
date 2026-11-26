// Host-side geometry test with the firmware's LVGL configuration and fonts.
#include <cassert>
#include <cstdio>
#include <lvgl.h>
#include "lang.h"
#include "ui.h"

uint32_t millis() { return 60000; }

static lv_obj_t* findLabel(lv_obj_t* obj, const char* text) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
  if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), text)) return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
    if (auto* match = findLabel(lv_obj_get_child(obj, i), text)) return match;
  return nullptr;
}

static lv_area_t bounds(const char* text) {
  auto* label = findLabel(lv_screen_active(), text);
  assert(label);
  lv_area_t area;
  lv_obj_get_coords(label, &area);
  return area;
}

static void check(const UiView& v, const char* caption) {
  ui::render(v);
  lv_obj_update_layout(lv_screen_active());
  const auto title = bounds("Solar");
  const auto label = bounds(caption);
  const auto power = bounds("0,0");
  const int above = label.y1 - title.y2 - 1;
  const int below = power.y1 - label.y2 - 1;
  std::fprintf(stderr, "%s: gaps %d/%d px (power y=%d)\n", caption, above, below, power.y1);
  assert(above >= 0 && below >= 0 && above - below >= -2 && above - below <= 2);
}

int main() {
  lv_init();
  lang::set(Lang::De);
  lv_tick_set_cb(millis);
  static uint16_t buffer[480 * 24];
  auto* display = lv_display_create(480, 480);
  lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  ui::begin(nullptr, nullptr, nullptr);

  UiView view;
  view.pageCount = 4;
  view.page = 3;
  view.solar = view.wifiConnected = view.state.valid = view.state.hasGrid = view.solarData.valid = true;
  view.stale = false;
  view.state.pvPower = 0;
  view.state.gridPower = 1600;
  view.solarMid = SOLAR_MID_FLOW;
  view.solarData.hourEnergy[12] = 1;
  view.hour = 23;
  check(view, "Erzeugung jetzt");
  view.solarMid = SOLAR_MID_PLANTS;
  view.solarData.plantCount = view.solarData.plantTotal = 2;
  check(view, "Erzeugung jetzt · 2 Anlagen");
  view.solarData.plantCount = view.solarData.plantTotal = 3;
  check(view, "Erzeugung jetzt · 3 Anlagen");
  view.state.pvPower = -1;
  ui::render(view);
  assert(!findLabel(lv_screen_active(), "Erzeugung jetzt · 3 Anlagen"));
  view.state.pvPower = 0;
  check(view, "Erzeugung jetzt · 3 Anlagen");
}
