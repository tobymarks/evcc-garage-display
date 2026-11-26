// Host-side touch test: no Wi-Fi, firmware or wallbox needed.
#include <assert.h>
#include <lvgl.h>

#include "lang.h"
#include "ui.h"

static uint32_t ticks = 60000;
static bool pressed;
static lv_point_t point;
static ChargeMode selected = ChargeMode::None;
static int selectionCount;

uint32_t millis() { return ticks; }

static void readTouch(lv_indev_t*, lv_indev_data_t* data) {
  data->point = point;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void touch(bool down, uint32_t duration) {
  pressed = down;
  for (uint32_t i = 0; i < duration; i += 20) {
    ticks += 20;
    lv_timer_handler();
  }
}

static lv_obj_t* findLabel(lv_obj_t* obj, const char* text) {
  if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), text)) return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    if (auto* match = findLabel(lv_obj_get_child(obj, i), text)) return match;
  }
  return nullptr;
}

static void aimAt(lv_obj_t* obj) {
  assert(obj);
  lv_obj_update_layout(obj);
  lv_area_t bounds;
  lv_obj_get_coords(obj, &bounds);
  point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
}

int main() {
  lang::set(Lang::De);
  lv_init();
  lv_tick_set_cb(millis);
  static uint16_t buffer[480 * 24];
  lv_display_t* display = lv_display_create(480, 480);
  lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  lv_indev_t* input = lv_indev_create();
  lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(input, readTouch);
  lv_indev_set_long_press_time(input, SETTINGS_LONG_PRESS_MS);
  ui::begin([](ChargeMode mode) { selected = mode; ++selectionCount; }, [] {}, [](int) {});

  UiView view;
  view.wifiConnected = true;
  view.stale = false;
  view.state.valid = true;
  view.state.mode = ChargeMode::Smart;
  view.pageCount = 2;
  ui::render(view);
  auto* stop = findLabel(lv_screen_active(), "Laden stoppen · 3 s halten");
  aimAt(stop);
  touch(true, 200);
  touch(false, 100);
  assert(selectionCount == 0);

  touch(true, SETTINGS_LONG_PRESS_MS + 1200);
  touch(false, 100);
  assert(selectionCount == 1 && selected == ChargeMode::Off);
  view.pendingMode = selected;
  ui::render(view);
  assert(!findLabel(lv_screen_active(), "Ladepunkt ist aus – Modus wählen"));
  assert(findLabel(lv_screen_active(), "Modus wird gesetzt …"));

  view.pendingMode = ChargeMode::None;
  view.state.mode = selected;  // exactly what the state poll has to return
  ui::render(view);
  assert(findLabel(lv_screen_active(), "Ladepunkt ist aus – Modus wählen"));

  aimAt(findLabel(lv_screen_active(), "Langsam"));
  touch(true, 100);
  touch(false, 100);
  assert(selectionCount == 2 && selected == ChargeMode::Slow);

  view.state.mode = ChargeMode::Smart;
  ui::render(view);
  aimAt(stop);
  touch(true, 300);
  ++view.page;
  ui::render(view);
  touch(true, SETTINGS_LONG_PRESS_MS + 200);
  touch(false, 100);
  assert(selectionCount == 2);

  aimAt(stop);
  touch(true, 200);
  point.x -= 80;  // Wischbewegung innerhalb der Stopptaste
  touch(true, SETTINGS_LONG_PRESS_MS + 200);
  touch(false, 100);
  assert(selectionCount == 2);
  aimAt(stop);

  for (int i = 0; i < 6; i++) {
    view.solar = i == 0;
    view.stale = i == 1;
    view.wifiConnected = i != 2;
    view.readOnly = i == 3;
    view.state.valid = i != 4;
    view.pendingMode = i == 5 ? ChargeMode::Fast : ChargeMode::None;
    ui::render(view);
    assert(lv_obj_has_flag(lv_obj_get_parent(stop), LV_OBJ_FLAG_HIDDEN));
    touch(true, SETTINGS_LONG_PRESS_MS + 200);
    touch(false, 100);
    assert(selectionCount == 2);
  }
  puts("Stop charging touch tests passed");
}
