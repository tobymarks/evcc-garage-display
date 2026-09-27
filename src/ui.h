#pragma once

#include "evcc_client.h"

struct UiView {
  EvccState state;
  bool wifiConnected = false;
  bool stale = true;                    // keine aktuellen Daten von EVCC
  ChargeMode pendingMode = ChargeMode::None;
  const char* flash = nullptr;          // kurzzeitige Fehlermeldung
  char clock[6] = "";
};

namespace ui {
using ModeCallback = void (*)(ChargeMode);
using Callback = void (*)();

void begin(ModeCallback onMode, Callback onSettingsRequested);
void setNight(bool night);
void render(const UiView& view);

// Vollbild-Hinweis (WLAN-Einrichtung, Verbindungsaufbau, Update …)
void showMessage(const char* icon, const char* title, const char* text);
void showMain();
// Antippen des Hinweisbildschirms (z. B. um die Einstellungen abzubrechen)
void onMessageTap(Callback cb);
}  // namespace ui
