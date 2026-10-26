#pragma once

#include "evcc_client.h"

struct UiView {
  EvccState state;
  bool wifiConnected = false;
  bool stale = true;                    // keine aktuellen Daten von evcc
  ChargeMode pendingMode = ChargeMode::None;
  const char* flash = nullptr;          // kurzzeitige Fehlermeldung
  char clock[6] = "";
  char lastUpdate[6] = "";              // Uhrzeit der letzten evcc-Daten
  uint8_t page = 0;                     // angezeigter Ladepunkt
  uint8_t pageCount = 1;                // > 1: Punkte unten, wischen zum Wechseln
};

namespace ui {
using ModeCallback = void (*)(ChargeMode);
using Callback = void (*)();
using SwipeCallback = void (*)(int delta);  // +1 = nächster, -1 = vorheriger Ladepunkt

void begin(ModeCallback onMode, Callback onSettingsRequested, SwipeCallback onSwipe);
void setNight(bool night);
void render(const UiView& view);

// Vollbild-Hinweis (WLAN-Einrichtung, Verbindungsaufbau, Update …)
void showMessage(const char* icon, const char* title, const char* text);
void showMain();
// Antippen des Hinweisbildschirms (z. B. um die Einstellungen abzubrechen)
void onMessageTap(Callback cb);
}  // namespace ui
