#pragma once

#include <Arduino.h>

struct Settings {
  String evccHost;
  uint16_t evccPort;
  uint8_t loadpoint;  // 1-basiert, wie in der evcc-API
  float latitude;
  float longitude;
  uint8_t rotation;   // 0–3 (× 90°)
  uint8_t theme;      // 0 = automatisch, 1 = hell, 2 = dunkel
  uint8_t screenTimeoutMin;  // 0 = Bildschirm immer an
  bool autoUpdate;           // neue Firmware selbst installieren
};

namespace settings {
void load();
void save();
Settings& get();
}  // namespace settings
