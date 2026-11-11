#pragma once

#include <Arduino.h>

// Die drei Modi am Display. evcc ab 0.316 kennt intern nur off/smart/now:
//   Smart  = smart + "immer laden" aus  → nur Sonnenüberschuss (heißt in evcc genauso)
//   Slow   = smart + "immer laden" an   → mindestens Mindestleistung, mehr bei Sonne
//   Fast   = now                        → volle Leistung
enum class ChargeMode : uint8_t { None, Smart, Slow, Fast };

struct EvccState {
  bool valid = false;
  bool missing = false;     // evcc kennt die eingestellte Ladepunkt-Nummer nicht
  uint32_t updatedMs = 0;

  char title[32] = "";
  ChargeMode mode = ChargeMode::None;
  bool modeOff = false;

  float chargePower = 0;    // W
  bool charging = false;
  bool connected = false;
  bool heating = false;     // Heizgerät (z. B. Wärmepumpe): "Ladestand" ist eine Temperatur
  float chargedEnergy = 0;  // Wh (aktuelle Sitzung)
  uint32_t chargeDuration = 0;  // s
  int phasesActive = 0;
  float sessionSolarPct = 0;  // 0..100
  float greenShare = -1;      // 0..1, aktueller Sonnenanteil der Ladeleistung
  float vehicleSoc = 0;       // %, bei Heizgeräten die Temperatur in °C
  float limitSoc = 0;         // %, Ladelimit des Ladepunkts (0 = keins), bei Heizgeräten die Zieltemperatur
  float vehicleLimitSoc = 0;  // %, im Fahrzeug eingestelltes Limit (0 = unbekannt)
  char vehicleName[32] = "";   // interner Name in evcc (leer = Gastfahrzeug)
  char vehicleTitle[32] = "";
  uint8_t vehicleCount = 0;   // in evcc angelegte Fahrzeuge (für Wechsel per Antippen)
  char vehicleNames[8][32] = {};
  char vehicleTitles[8][32] = {};
  char pvAction[12] = "";
  uint32_t pvRemaining = 0;   // s
  bool planActive = false;
  bool smartCostActive = false;
  bool hasBoost = false;      // evcc kennt Battery Boost
  bool batteryBoost = false;  // Hausakku hilft im Modus Smart beim Laden

  // Hausdaten – damit man ohne Fahrzeug sieht, ob sich Anstecken gerade lohnt
  float pvPower = -1;         // W, -1 = keine PV-Anlage bekannt
  float homePower = 0;        // W, Verbrauch im Haus ohne Ladepunkte
  float batterySoc = -1;      // %, -1 = kein Hausakku
  float batteryPower = 0;     // W wie in evcc: + = Akku entlädt, − = Akku lädt
  float prioritySoc = 0;      // %, bis hierhin geht die Sonne zuerst in den Hausakku (0 = Fahrzeug zuerst)
  float gridPower = 0;        // W wie in evcc: + = Netzbezug, − = Einspeisung
  bool hasGrid = false;       // evcc kennt die Netzleistung
  float minChargePower = 0;   // W, ab diesem Überschuss startet Smart (Mindeststrom × Phasen × 230 V)
  float solarToday = -1;      // Wh, Solarprognose für den Rest des Tages (-1 = keine Prognose)
  float solarTomorrow = -1;   // Wh, Solarprognose für morgen
};

enum class CommandResult : uint8_t { None, Ok, Failed };

// Ladepunkte werden über ihre Position in den Einstellungen angesprochen (0 … count()-1)
namespace evcc {
void begin();
uint8_t count();
// Kopiert den neuesten Zustand aller Ladepunkte; true, wenn er sich seit dem letzten Aufruf geändert hat.
bool latest(EvccState* out);
void requestMode(uint8_t index, ChargeMode mode);
CommandResult takeCommandResult(uint8_t index);
void requestBoost(uint8_t index, bool on);
CommandResult takeBoostResult(uint8_t index);
// Fahrzeug am Ladepunkt setzen (leerer Name = Gastfahrzeug / Zuordnung lösen)
void requestVehicle(uint8_t index, const char* name);
CommandResult takeVehicleResult(uint8_t index);
}  // namespace evcc
