// Rendert die Oberfläche auf dem Rechner – für Screenshots der Website und zum Prüfen von
// Layoutänderungen ohne Gerät. Aufruf über tools/sim/render.sh
#include <lvgl.h>

#include <string>

#include "icons.h"
#include "preview.h"
#include "ui.h"

static constexpr int W = 480, H = 480;
static uint16_t fb[W * H];
static uint32_t now = 60000;  // nach dem Start, damit "Verbinde …" nicht erscheint

uint32_t millis() { return now; }

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const uint16_t* src = (const uint16_t*)px;
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) fb[y * W + x] = *src++;
  lv_display_flush_ready(d);
}

static void writeBmp(const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  const uint32_t row = W * 3, size = 54 + row * H;
  uint8_t h[54] = {'B', 'M'};
  memcpy(h + 2, &size, 4);
  h[10] = 54;
  h[14] = 40;
  const int32_t w = W, hh = -H;  // von oben nach unten
  memcpy(h + 18, &w, 4);
  memcpy(h + 22, &hh, 4);
  h[26] = 1;
  h[28] = 24;
  fwrite(h, 1, 54, f);
  for (int i = 0; i < W * H; i++) {
    const uint16_t c = fb[i];
    const uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    const uint8_t px[3] = {(uint8_t)((b << 3) | (b >> 2)), (uint8_t)((g << 2) | (g >> 4)),
                           (uint8_t)((r << 3) | (r >> 2))};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}

// sim <Ausgabeordner> <Zustand>… – je Zustand <Zustand>-light.bmp und <Zustand>-dark.bmp
int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "Aufruf: sim <Ordner> <Zustand>…  (%s)\n", preview::NAMES);
    return 1;
  }
  lv_init();
  lv_tick_set_cb(millis);
  static uint16_t buf[W * H];
  lv_display_t* d = lv_display_create(W, H);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(d, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(d, flush);

  ui::begin([](ChargeMode) {}, []() {});
  for (int i = 2; i < argc; i++) {
    UiView v;
    bool stale;
    // "settings": Hinweis beim Öffnen der Einstellungen (Text wie in main.cpp)
    const bool settings = !strcmp(argv[i], "settings");
    if (settings)
      ui::showMessage(ICON_SETTINGS, "Einstellungen",
                      "Mit dem WLAN \"evcc-Garage\" verbinden. Die Seite öffnet sich automatisch, "
                      "sonst 192.168.4.1 aufrufen. Antippen zum Abbrechen.");
    else if (!preview::scene(argv[i], v.state, stale)) {
      fprintf(stderr, "Unbekannter Zustand: %s\n", argv[i]);
      return 1;
    }
    v.wifiConnected = true;
    v.stale = stale;
    strcpy(v.clock, "14:32");
    strcpy(v.lastUpdate, "14:29");
    for (bool dark : {false, true}) {
      ui::setNight(dark);
      if (!settings) {
        ui::showMain();
        ui::render(v);
      }
      now += 1000;
      lv_obj_invalidate(lv_screen_active());
      lv_refr_now(nullptr);
      writeBmp(std::string(argv[1]) + "/" + argv[i] + (dark ? "-dark" : "-light") + ".bmp");
    }
  }
  return 0;
}
