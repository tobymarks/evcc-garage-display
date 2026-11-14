#pragma once

#include <Arduino.h>

class WebServer;

namespace display {
// Initialisiert Display, Touch und LVGL. Kalibriert den Touch beim ersten Start
// oder wenn beim Einschalten der Bildschirm berührt wird.
// backlightHz ist die Taktung der Hintergrundbeleuchtung ab dem Start.
void begin(uint8_t rotation, uint32_t backlightHz);

void setBrightness(uint8_t value);

// Taktung der Hintergrundbeleuchtung im laufenden Betrieb umstellen (#21), gilt bis zum Neustart.
// Liefert die tatsächlich eingestellte Frequenz, 0 wenn sie sich nicht einstellen ließ.
uint32_t setBacklightHz(uint32_t hz);
uint32_t backlightHz();

// Bei ausgeschaltetem Bildschirm weckt die erste Berührung nur auf und löst nichts aus.
void setSleeping(bool sleeping);
bool takeWakeRequest();
uint32_t lastTouchMs();

// Kennung, Firmware und Auflösung des Touch-Controllers (für die Diagnose)
String touchInfo();

// Sendet den aktuellen Bildschirminhalt als BMP (zum Prüfen der Oberfläche aus der Ferne)
void sendScreenshot(WebServer& server);
}  // namespace display
