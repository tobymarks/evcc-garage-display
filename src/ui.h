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
  bool vehicleSwitch = true;            // Fahrzeugwechsel per Antippen erlaubt (Einstellung)
  bool limitButton = false;             // Ladelimit neben dem Fahrzeug zeigen und einstellen (Einstellung)
  float pendingLimit = -1;              // angefragtes Ladelimit (% bzw. kWh), bis evcc es bestätigt; -1 = keins
};

namespace ui {
using ModeCallback = void (*)(ChargeMode);
using Callback = void (*)();
using SwipeCallback = void (*)(int delta);  // +1 = nächster, -1 = vorheriger Ladepunkt
// Ladelimit gewählt: in Prozent oder (energy) als Energie dieser Ladung in kWh, 0 = kein Limit
using LimitCallback = void (*)(bool energy, float value);

void begin(ModeCallback onMode, Callback onSettingsRequested, SwipeCallback onSwipe, Callback onBoost = nullptr,
           Callback onVehicle = nullptr, LimitCallback onLimit = nullptr);
// Auswahl des Ladelimits öffnen bzw. schließen (öffnet sich sonst per Antippen des Limits)
void showLimitPicker(bool show);
void setNight(bool night);
void render(const UiView& view);

// Vollbild-Hinweis (WLAN-Einrichtung, Verbindungsaufbau, Update …)
void showMessage(const char* icon, const char* title, const char* text);
void showMain();
// Antippen des Hinweisbildschirms (z. B. um die Einstellungen abzubrechen)
void onMessageTap(Callback cb);
}  // namespace ui
