#include "display.h"

#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

#include "board.h"
#include "config.h"

static esp_lcd_panel_handle_t panel = nullptr;
static uint8_t rotation = 0;  // 0–3, jeweils 90° im Uhrzeigersinn

static constexpr int BUF_LINES = 48;
static constexpr size_t BUF_BYTES = LCD_WIDTH * BUF_LINES * sizeof(uint16_t);
static uint8_t* rotateBuf = nullptr;

static volatile bool sleeping = false;
static volatile bool wakeRequested = false;
static bool swallowTouch = false;
static uint32_t lastTouch = 0;

// ---------- ST7701S-Initialisierung (3-Draht-SPI, 9 Bit: D/C + 8 Datenbits) ----------

static void spiWrite9(bool data, uint8_t value) {
  const uint16_t word = (data ? 0x100 : 0) | value;
  for (int bit = 8; bit >= 0; bit--) {
    digitalWrite(PIN_LCD_SPI_SCK, LOW);
    digitalWrite(PIN_LCD_SPI_MOSI, (word >> bit) & 1);
    delayMicroseconds(1);
    digitalWrite(PIN_LCD_SPI_SCK, HIGH);
    delayMicroseconds(1);
  }
}

static void lcdCommand(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0) {
  digitalWrite(PIN_LCD_SPI_CS, LOW);
  spiWrite9(false, cmd);
  for (size_t i = 0; i < len; i++) spiWrite9(true, data[i]);
  digitalWrite(PIN_LCD_SPI_CS, HIGH);
}

// Format: Befehl, Anzahl Datenbytes, Datenbytes …  (Herstellersequenz "st7701_type9" aus Arduino_GFX)
static const uint8_t ST7701_INIT[] = {
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,  // Befehlsseite 0
    0xC0, 2, 0x3B, 0x00,
    0xC1, 2, 0x0D, 0x02,
    0xC2, 2, 0x31, 0x05,
    0xCD, 1, 0x00,
    0xB0, 16, 0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18,
    0xB1, 16, 0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,  // Befehlsseite 1
    0xB0, 1, 0x60,
    0xB1, 1, 0x32,
    0xB2, 1, 0x07,
    0xB3, 1, 0x80,
    0xB5, 1, 0x49,
    0xB7, 1, 0x85,
    0xB8, 1, 0x21,
    0xC1, 1, 0x78,
    0xC2, 1, 0x78,
    0xE0, 3, 0x00, 0x1B, 0x02,
    0xE1, 11, 0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44,
    0xE2, 12, 0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00,
    0xE3, 4, 0x00, 0x00, 0x11, 0x11,
    0xE4, 2, 0x44, 0x44,
    0xE5, 16, 0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0,
    0xE6, 4, 0x00, 0x00, 0x11, 0x11,
    0xE7, 2, 0x44, 0x44,
    0xE8, 16, 0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0,
    0xEB, 7, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40,
    0xEC, 2, 0x3C, 0x00,
    0xED, 16, 0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,  // Befehlsseite 3
    0xE5, 1, 0xE4,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,  // zurück zu Seite 0
    0x3A, 1, 0x60,                          // Pixelformat laut Hersteller
};

static void initST7701() {
  pinMode(PIN_LCD_SPI_CS, OUTPUT);
  pinMode(PIN_LCD_SPI_SCK, OUTPUT);
  pinMode(PIN_LCD_SPI_MOSI, OUTPUT);
  digitalWrite(PIN_LCD_SPI_CS, HIGH);
  digitalWrite(PIN_LCD_SPI_SCK, LOW);

  lcdCommand(0x01);  // Software-Reset
  delay(120);
  for (size_t i = 0; i < sizeof(ST7701_INIT);) {
    const uint8_t cmd = ST7701_INIT[i];
    const uint8_t len = ST7701_INIT[i + 1];
    lcdCommand(cmd, &ST7701_INIT[i + 2], len);
    i += 2 + len;
  }
  lcdCommand(0x11);  // Sleep out
  delay(120);
  lcdCommand(0x29);  // Display on
}

// ---------- RGB-Schnittstelle ----------

static void initRgbPanel() {
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = LCD_PCLK_HZ;
  cfg.timings.h_res = LCD_WIDTH;
  cfg.timings.v_res = LCD_HEIGHT;
  cfg.timings.hsync_pulse_width = LCD_HSYNC_PULSE;
  cfg.timings.hsync_back_porch = LCD_HSYNC_BACK;
  cfg.timings.hsync_front_porch = LCD_HSYNC_FRONT;
  cfg.timings.vsync_pulse_width = LCD_VSYNC_PULSE;
  cfg.timings.vsync_back_porch = LCD_VSYNC_BACK;
  cfg.timings.vsync_front_porch = LCD_VSYNC_FRONT;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  // Zwei Zwischenpuffer im internen RAM: Die DMA liest daraus statt direkt aus dem PSRAM.
  // Das entkoppelt die Bildausgabe von PSRAM-Last (WLAN) – die Ursache fürs Verschieben.
  cfg.bounce_buffer_size_px = LCD_WIDTH * 10;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = PIN_LCD_HSYNC;
  cfg.vsync_gpio_num = PIN_LCD_VSYNC;
  cfg.de_gpio_num = PIN_LCD_DE;
  cfg.pclk_gpio_num = PIN_LCD_PCLK;
  cfg.disp_gpio_num = -1;
  const int data[16] = LCD_DATA_PINS;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
  cfg.flags.fb_in_psram = 1;

  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
}

// ---------- Touch GT911 ----------

static uint8_t touchAddr = 0;
static bool touchDown = false;
static uint16_t touchX = 0, touchY = 0;

static bool gtRead(uint16_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(touchAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(touchAddr, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void gtWrite(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(touchAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(value);
  Wire.endTransmission();
}

static void initTouch() {
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);
  for (uint8_t addr : {0x5D, 0x14}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      touchAddr = addr;
      break;
    }
  }
  if (!touchAddr) log_e("GT911 nicht gefunden");
}

// Liefert physische Panel-Koordinaten; der Zustand bleibt, solange kein neuer Messwert vorliegt.
static bool readTouch(uint16_t& x, uint16_t& y) {
  if (!touchAddr) return false;
  uint8_t status;
  if (gtRead(0x814E, &status, 1) && (status & 0x80)) {
    const uint8_t points = status & 0x0F;
    uint8_t p[4];
    touchDown = points > 0 && gtRead(0x8150, p, sizeof(p));
    if (touchDown) {
      touchX = p[0] | (p[1] << 8);
      touchY = p[2] | (p[3] << 8);
    }
    gtWrite(0x814E, 0);
  }
  x = touchX;
  y = touchY;
  return touchDown;
}

// ---------- Drehung ----------

// Logische Koordinate (wie LVGL sie sieht) → physischer Panel-Pixel
static void logicalToPhysical(int x, int y, int& px, int& py) {
  switch (rotation) {
    case 1: px = LCD_WIDTH - 1 - y; py = x; break;
    case 2: px = LCD_WIDTH - 1 - x; py = LCD_HEIGHT - 1 - y; break;
    case 3: px = y; py = LCD_HEIGHT - 1 - x; break;
    default: px = x; py = y; break;
  }
}

static void physicalToLogical(int px, int py, int& x, int& y) {
  switch (rotation) {
    case 1: x = py; y = LCD_WIDTH - 1 - px; break;
    case 2: x = LCD_WIDTH - 1 - px; y = LCD_HEIGHT - 1 - py; break;
    case 3: x = LCD_HEIGHT - 1 - py; y = px; break;
    default: x = px; y = py; break;
  }
}

// ---------- LVGL ----------

static uint32_t tickCb() { return millis(); }

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  lv_area_t a = *area;
  const lv_display_rotation_t rot = lv_display_get_rotation(disp);
  if (rot != LV_DISPLAY_ROTATION_0) {
    const lv_color_format_t cf = lv_display_get_color_format(disp);
    const uint32_t w = lv_area_get_width(area);
    const uint32_t h = lv_area_get_height(area);
    lv_display_rotate_area(disp, &a);
    lv_draw_sw_rotate(px, rotateBuf, w, h, lv_draw_buf_width_to_stride(w, cf),
                      lv_draw_buf_width_to_stride(lv_area_get_width(&a), cf), rot, cf);
    px = rotateBuf;
  }
  // Kopiert in den Framebuffer im PSRAM; danach ist der LVGL-Puffer wieder frei
  esp_lcd_panel_draw_bitmap(panel, a.x1, a.y1, a.x2 + 1, a.y2 + 1, px);
  lv_display_flush_ready(disp);
}

static void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  uint16_t px, py;
  const bool touched = readTouch(px, py);
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
    int x, y;
    physicalToLogical(px, py, x, y);
    data->point.x = x;
    data->point.y = y;
  }
}

// ---------- öffentliche Funktionen ----------

void display::begin(uint8_t rot) {
  rotation = rot & 3;

  ledcAttach(PIN_LCD_BACKLIGHT, 20000, 8);
  ledcWrite(PIN_LCD_BACKLIGHT, 0);

  initST7701();
  initRgbPanel();
  initTouch();

  lv_init();
  lv_tick_set_cb(tickCb);

  lv_display_t* disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(disp, flushCb);
  void* buf1 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void* buf2 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_display_set_buffers(disp, buf1, buf2, BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
  if (rotation) {
    rotateBuf = (uint8_t*)heap_caps_malloc(BUF_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_display_set_rotation(disp, (lv_display_rotation_t)rotation);
  }

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
  lv_indev_set_long_press_time(indev, SETTINGS_LONG_PRESS_MS);
}

void display::setBrightness(uint8_t value) { ledcWrite(PIN_LCD_BACKLIGHT, value); }

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
  void* fb = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || !fb) {
    server.send(500, "text/plain", "kein Framebuffer\n");
    return;
  }
  const uint16_t* pixels = static_cast<const uint16_t*>(fb);
  const int w = LCD_WIDTH, h = LCD_HEIGHT;
  const uint32_t rowBytes = w * 3;  // 1440 – durch 4 teilbar, kein Padding nötig

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

  // Ausgabe in logischer Ausrichtung (so, wie man das Display ansieht), BMP von unten nach oben
  static uint8_t row[LCD_WIDTH * 3];
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      int px, py;
      logicalToPhysical(x, y, px, py);
      const uint16_t c = pixels[py * LCD_WIDTH + px];
      row[x * 3 + 0] = (c & 0x1F) << 3;          // B
      row[x * 3 + 1] = ((c >> 5) & 0x3F) << 2;   // G
      row[x * 3 + 2] = ((c >> 11) & 0x1F) << 3;  // R
    }
    client.write(row, rowBytes);
  }
}
