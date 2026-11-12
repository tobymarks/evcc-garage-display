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
  bool boostButton = false;             // Akku-Anzeige schaltet Battery Boost (Einstellung)
  int8_t pendingBoost = -1;             // angefragter Boost (0/1), bis evcc ihn bestätigt
  bool pendingVehicle = false;          // Fahrzeugwechsel angefragt, bis evcc ihn bestätigt
  char pendingVehicleTitle[32] = "";
  bool hasVehicles = false;             // in evcc sind Fahrzeuge angelegt – die Fahrzeugzeile lässt sich antippen
};

namespace ui {
using ModeCallback = void (*)(ChargeMode);
using Callback = void (*)();
using SwipeCallback = void (*)(int delta);  // +1 = nächster, -1 = vorheriger Ladepunkt

void begin(ModeCallback onMode, Callback onSettingsRequested, SwipeCallback onSwipe, Callback onBoost = nullptr,
           Callback onVehicle = nullptr);
void setNight(bool night);
void render(const UiView& view);

// Vollbild-Hinweis (WLAN-Einrichtung, Verbindungsaufbau, Update …)
void showMessage(const char* icon, const char* title, const char* text);
void showMain();
// Antippen des Hinweisbildschirms (z. B. um die Einstellungen abzubrechen)
void onMessageTap(Callback cb);
}  // namespace ui
