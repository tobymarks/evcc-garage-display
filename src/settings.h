#pragma once

#include <Arduino.h>

struct Settings {
  String evccHost;
  uint16_t evccPort;
  uint8_t loadpoint;  // 1-basiert, wie in der EVCC-API
  float latitude;
  float longitude;
  uint8_t rotation;   // 1 oder 3
  uint8_t theme;      // 0 = automatisch, 1 = hell, 2 = dunkel
  bool hasTouchCal;
  uint16_t touchCal[8];
};

namespace settings {
void load();
void save();
void saveTouchCal(const uint16_t cal[8]);
void clearTouchCal();
Settings& get();
}  // namespace settings
