#pragma once

#include <Arduino.h>

#include "config.h"

struct Settings {
  String evccHost;
  uint16_t evccPort;
  uint8_t loadpoints[MAX_LOADPOINTS];  // Nummern 1-basiert, wie in der evcc-API
  uint8_t loadpointCount;              // ≥ 1; bei mehreren wird zwischen ihnen gewischt
  float latitude;
  float longitude;
  uint8_t rotation;   // 0–3 (× 90°)
  uint8_t theme;      // 0 = automatisch, 1 = hell, 2 = dunkel
  uint8_t screenTimeoutMin;  // 0 = Bildschirm immer an
  bool autoUpdate;           // neue Firmware selbst installieren
};

namespace settings {
void load();
// "1, 3" → {1, 3}; ungültige und doppelte Nummern fallen weg, leer → Ladepunkt 1
void parseLoadpoints(const char* text, Settings& s);
// {1, 3} → "1,3"
String loadpointsText(const Settings& s);
void save();
Settings& get();
}  // namespace settings
