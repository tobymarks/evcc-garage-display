#pragma once

#include <Arduino.h>

class WebServer;

namespace display {
// Initialisiert Display, Touch und LVGL. Kalibriert den Touch beim ersten Start
// oder wenn beim Einschalten der Bildschirm berührt wird.
void begin(uint8_t rotation);

void setBrightness(uint8_t value);

// Bei ausgeschaltetem Bildschirm weckt die erste Berührung nur auf und löst nichts aus.
void setSleeping(bool sleeping);
bool takeWakeRequest();
uint32_t lastTouchMs();

// Sendet den aktuellen Bildschirminhalt als BMP (zum Prüfen der Oberfläche aus der Ferne)
void sendScreenshot(WebServer& server);
}  // namespace display
