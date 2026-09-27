#include "display.h"

#include <WebServer.h>
#include <lvgl.h>

#include "config.h"
#include "lgfx_cyd.h"
#include "settings.h"

static LGFX tft;

static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 240;
static constexpr int BUF_LINES = 24;
static constexpr size_t BUF_BYTES = SCREEN_W * BUF_LINES * sizeof(uint16_t);

static volatile bool sleeping = false;
static volatile bool wakeRequested = false;
static bool swallowTouch = false;
static uint32_t lastTouch = 0;

static uint32_t tickCb() { return millis(); }

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  tft.pushImage(area->x1, area->y1, w, h, reinterpret_cast<lgfx::rgb565_t*>(px));
  lv_display_flush_ready(disp);
}

static void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  uint16_t x, y;
  const bool touched = tft.getTouch(&x, &y);
  if (touched) lastTouch = millis();

  // Erste Berührung bei dunklem Bildschirm nur zum Aufwecken verwenden,
  // bis der Finger wieder losgelassen wird.
  if (touched && sleeping) {
    wakeRequested = true;
    swallowTouch = true;
  }
  if (swallowTouch) {
    if (!touched) swallowTouch = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  data->state = touched ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (touched) {
    data->point.x = x;
    data->point.y = y;
  }
}

static void calibrateTouch() {
  uint16_t cal[8];
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(lgfx::middle_center);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.drawString("Touch kalibrieren:", SCREEN_W / 2, SCREEN_H / 2 - 12);
  tft.drawString("Pfeilspitzen in den Ecken antippen", SCREEN_W / 2, SCREEN_H / 2 + 12);
  tft.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 24);
  settings::saveTouchCal(cal);
  tft.fillScreen(TFT_BLACK);
}

void display::begin(uint8_t rotation) {
  tft.init();
  tft.setRotation(rotation);
  tft.setBrightness(BRIGHTNESS);
  tft.fillScreen(TFT_BLACK);

  Settings& s = settings::get();
  // Finger beim Einschalten auf dem Display → neu kalibrieren
  bool recalibrate = !s.hasTouchCal;
  if (!recalibrate) {
    tft.setTouchCalibrate(s.touchCal);
    uint16_t x, y;
    const uint32_t start = millis();
    while (millis() - start < 300) {
      if (tft.getTouch(&x, &y)) {
        recalibrate = true;
        break;
      }
      delay(10);
    }
  }
  if (recalibrate) calibrateTouch();

  lv_init();
  lv_tick_set_cb(tickCb);

  lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, flushCb);
  void* buf1 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* buf2 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(disp, buf1, buf2, BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
  lv_indev_set_long_press_time(indev, SETTINGS_LONG_PRESS_MS);
}

void display::setBrightness(uint8_t value) { tft.setBrightness(value); }

void display::setSleeping(bool value) { sleeping = value; }

bool display::takeWakeRequest() {
  const bool w = wakeRequested;
  wakeRequested = false;
  return w;
}

uint32_t display::lastTouchMs() { return lastTouch; }

static void put16(uint8_t* p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}

static void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = v >> (8 * i);
}

void display::sendScreenshot(WebServer& server) {
  const int w = tft.width(), h = tft.height();
  const uint32_t rowBytes = w * 3;  // 960 – bereits durch 4 teilbar, kein Padding nötig
  uint8_t hdr[54] = {'B', 'M'};
  put32(hdr + 2, sizeof(hdr) + rowBytes * h);
  put32(hdr + 10, sizeof(hdr));
  put32(hdr + 14, 40);
  put32(hdr + 18, w);
  put32(hdr + 22, h);
  put16(hdr + 26, 1);
  put16(hdr + 28, 24);
  put32(hdr + 34, rowBytes * h);

  server.setContentLength(sizeof(hdr) + rowBytes * h);
  server.send(200, "image/bmp", "");
  WiFiClient client = server.client();
  client.write(hdr, sizeof(hdr));
  static lgfx::rgb888_t row[320];
  for (int y = h - 1; y >= 0; y--) {  // BMP speichert von unten nach oben
    tft.readRect(0, y, w, 1, row);
    client.write(reinterpret_cast<uint8_t*>(row), rowBytes);
  }
}
