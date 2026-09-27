#pragma once

#include <Arduino.h>

// Die drei Modi am Display. EVCC ab 0.316 kennt intern nur off/smart/now:
//   PV     = smart + "immer laden" aus  → nur Sonnenüberschuss
//   Smart  = smart + "immer laden" an   → mindestens Mindestleistung, mehr bei Sonne
//   Fast   = now                        → volle Leistung
enum class ChargeMode : uint8_t { None, PV, Smart, Fast };

struct EvccState {
  bool valid = false;
  uint32_t updatedMs = 0;

  char title[32] = "";
  ChargeMode mode = ChargeMode::None;
  bool modeOff = false;

  float chargePower = 0;    // W
  bool charging = false;
  bool connected = false;
  float chargedEnergy = 0;  // Wh (aktuelle Sitzung)
  uint32_t chargeDuration = 0;  // s
  int phasesActive = 0;
  float sessionSolarPct = 0;  // 0..100
  float greenShare = -1;      // 0..1, aktueller Sonnenanteil der Ladeleistung
  float vehicleSoc = 0;       // %
  char vehicleTitle[32] = "";
  char pvAction[12] = "";
  uint32_t pvRemaining = 0;   // s
  bool planActive = false;
  bool smartCostActive = false;
};

enum class CommandResult : uint8_t { None, Ok, Failed };

namespace evcc {
void begin();
// Kopiert den neuesten Zustand; true, wenn er sich seit dem letzten Aufruf geändert hat.
bool latest(EvccState& out);
void requestMode(ChargeMode mode);
CommandResult takeCommandResult();
}  // namespace evcc
