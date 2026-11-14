#pragma once

#include <Arduino.h>

// Die drei Modi am Display. evcc ab 0.316 kennt intern nur off/smart/now:
//   Smart  = smart + "immer laden" aus  → nur Sonnenüberschuss (heißt in evcc genauso)
//   Slow   = smart + "immer laden" an   → mindestens Mindestleistung, mehr bei Sonne
//   Fast   = now                        → volle Leistung
enum class ChargeMode : uint8_t { None, Smart, Slow, Fast };

#define VEHICLE_NAME_LEN 48  // interner Name eines Fahrzeugs in evcc, mit Abschluss
#define MAX_VEHICLES 16      // mehr Fahrzeuge lassen sich nicht durchtippen

struct EvccState {
  bool valid = false;
  bool missing = false;     // evcc kennt die eingestellte Ladepunkt-Nummer nicht
  uint32_t updatedMs = 0;
  uint32_t polledMs = 0;    // Start der Abfrage, aus der dieser Zustand stammt

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
  float limitEnergy = 0;      // kWh, Energielimit dieser Ladung (0 = keins)
  // evcc kennt den Ladestand des Fahrzeugs: das Ladelimit gilt in Prozent. Sonst (Fahrzeug ohne
  // Schnittstelle, Gastfahrzeug) lässt sich die Ladung nur über die Energie in kWh begrenzen.
  bool socBased = false;
  char vehicleName[VEHICLE_NAME_LEN] = "";  // interner Name in evcc (leer = Gastfahrzeug)
  char vehicleTitle[32] = "";
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
  float solarScale = 1;       // Faktor, mit dem evcc die Prognose an die echte Erzeugung anpasst; schon eingerechnet
};

// Solarseite: Erzeugung je Stunde und je Anlage. Die Stundenwerte liefert evcc unter
// /api/history/energy (neuere Versionen); ohne sie bleibt valid false und die Seite zeigt nur,
// was der normale Zustand hergibt.
#define MAX_PV_PLANTS 3
struct EvccSolar {
  bool valid = false;             // Stundenwerte von heute liegen vor
  float hourEnergy[24] = {};      // kWh, je Stunde heute erzeugt (alle Anlagen)
  float hourForecast[24] = {};    // kWh, Prognose je Stunde heute
  float today = 0;                // kWh, heute bisher erzeugt
  float forecastToday = 0;        // kWh, Prognose für den ganzen Tag
  uint8_t plantTotal = 0;         // PV-Anlagen in evcc
  uint8_t plantCount = 0;         // Zeilen in plantTitle/plantPower; bei mehr Anlagen fasst die letzte den Rest zusammen
  char plantTitle[MAX_PV_PLANTS][24] = {};
  float plantPower[MAX_PV_PLANTS] = {};  // W
};

// In evcc angelegte Fahrzeuge (für den Wechsel per Antippen) – gelten für alle Ladepunkte
struct EvccVehicles {
  uint8_t count = 0;
  char names[MAX_VEHICLES][VEHICLE_NAME_LEN] = {};
  char titles[MAX_VEHICLES][32] = {};
  bool offline[MAX_VEHICLES] = {};  // evcc bekommt von diesem Fahrzeug keinen Ladestand
};

enum class CommandResult : uint8_t { None, Ok, Failed };
enum class Command : uint8_t { Mode, Boost, Vehicle, Limit, Count };

// Zuletzt ausgeführter Befehl einer Art an einem Ladepunkt
struct CommandStatus {
  uint32_t id = 0;  // Nummer aus request…(), 0 = noch keiner
  CommandResult result = CommandResult::None;
  uint32_t doneMs = 0;  // Zeitpunkt, zu dem evcc geantwortet hat
};

// Verlauf der Abfragen seit dem Start (für die Diagnose)
struct EvccDiagnostics {
  char version[24] = "";    // evcc-Version, leer = unbekannt
  uint32_t polls = 0;       // Abfragen des Zustands
  uint32_t failed = 0;      // davon ohne brauchbare Antwort
  int lastCode = 0;         // HTTP-Code der letzten Abfrage, negativ = Netzwerkfehler, 0 = noch keine
  uint32_t lastDurationMs = 0;
  uint32_t maxDurationMs = 0;
  uint32_t lastOkMs = 0;    // millis() der letzten brauchbaren Antwort, 0 = noch keine
};

// Ladepunkte werden über ihre Position in den Einstellungen angesprochen (0 … count()-1)
namespace evcc {
using Diagnostics = EvccDiagnostics;
void begin();
uint8_t count();
// Kopiert den neuesten Zustand aller Ladepunkte, die Fahrzeugliste und die Solardaten, sofern sich
// seit dem letzten Aufruf etwas geändert hat; dann true.
bool latest(EvccState* out, EvccVehicles& vehicles, EvccSolar& solar);
// Befehle laufen im Netzwerk-Task; je Art und Ladepunkt zählt nur der zuletzt angefragte.
// Rückgabe: Nummer der Anfrage, zum Abgleich mit status().
uint32_t requestMode(uint8_t index, ChargeMode mode);
uint32_t requestBoost(uint8_t index, bool on);
// Fahrzeug am Ladepunkt setzen (leerer Name = Gastfahrzeug / Zuordnung lösen)
uint32_t requestVehicle(uint8_t index, const char* name);
// Ladelimit setzen: in Prozent (limitsoc) oder als Energie dieser Ladung in kWh (limitenergy, 0 = keins)
uint32_t requestLimit(uint8_t index, bool energy, float value);
CommandStatus status(Command command, uint8_t index);
// Kleinster bisher freier Stack des Netzwerk-Tasks in Byte (für /diag)
uint32_t stackFree();
// Für die Diagnose: evcc-Version und wie die Abfragen laufen
Diagnostics diagnostics();
}  // namespace evcc
