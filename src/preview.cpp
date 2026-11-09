#include "preview.h"

#include <string.h>

const char* const preview::NAMES = "charging|solar|fast|nocar|nocarlow|nocarfull|night|evening|priority|nightfast|waiting|limit|boostready|boost|start|off|stale|heating|heatingdone";

// Typisches Haus am Mittag: PV 6,2 kW, Haus 0,9 kW, Akku 78 % lädt mit 5 kW, 0,3 kW gehen ins Netz;
// Smart braucht einphasig 1,4 kW. Beim Laden geht die Bilanz mit der Ladeleistung auf (Energiebalken).
static void house(EvccState& p, float pv, float home, float battery, float grid) {
  p.pvPower = pv;
  p.homePower = home;
  p.batteryPower = battery;
  p.gridPower = grid;
  p.hasGrid = true;
  p.batterySoc = 78;
  p.minChargePower = 6 * 230;
  p.solarToday = 14200;
  p.solarTomorrow = 8200;
}

bool preview::scene(const char* name, EvccState& p, bool& stale) {
  p = EvccState();
  p.valid = true;
  strncpy(p.title, "Garage", sizeof(p.title) - 1);
  p.connected = true;
  p.mode = ChargeMode::Slow;
  strncpy(p.vehicleTitle, "ID.4", sizeof(p.vehicleTitle) - 1);
  p.vehicleSoc = 41;
  p.hasBoost = true;
  stale = false;
  house(p, 6200, 900, -5000, -300);

  if (!strcmp(name, "charging") || !strcmp(name, "stale")) {
    p.charging = true;
    p.chargePower = 7360;
    p.phasesActive = 3;
    p.greenShare = 0.72f;
    house(p, 6200, 900, 0, 2060);
    p.chargedEnergy = 12400;
    p.chargeDuration = 5820;
    p.sessionSolarPct = 48;
    p.vehicleSoc = 64;
    stale = !strcmp(name, "stale");
  } else if (!strcmp(name, "solar")) {
    p.mode = ChargeMode::Smart;
    p.charging = true;
    p.chargePower = 4140;
    p.phasesActive = 1;
    p.greenShare = 1.0f;
    house(p, 6200, 900, -1160, 0);
    p.chargedEnergy = 2900;
    p.chargeDuration = 1680;
    p.sessionSolarPct = 97;
  } else if (!strcmp(name, "fast")) {
    p.mode = ChargeMode::Fast;
    p.charging = true;
    p.chargePower = 11040;
    p.phasesActive = 3;
    p.greenShare = 0.04f;
    house(p, 1340, 900, 0, 10600);
    p.chargedEnergy = 7400;
    p.chargeDuration = 2460;
    p.sessionSolarPct = 6;
  } else if (!strcmp(name, "nocar")) {
    p.connected = false;
  } else if (!strcmp(name, "nocarlow")) {
    p.connected = false;
    p.mode = ChargeMode::Smart;
    house(p, 1900, 1100, -800, 0);
  } else if (!strcmp(name, "nocarfull")) {
    p.connected = false;
    house(p, 6200, 900, 0, -5300);
    p.batterySoc = 100;
  } else if (!strcmp(name, "night")) {
    p.connected = false;
    house(p, 0, 450, 350, 100);
    p.solarToday = 0;
  } else if (!strcmp(name, "evening")) {
    // Kurz vor Sonnenuntergang: kleine Anteile von Netz, Sonne und Akku
    p.connected = false;
    p.mode = ChargeMode::Smart;
    house(p, 400, 1200, 600, 200);
    p.solarToday = 300;
  } else if (!strcmp(name, "priority")) {
    // Hausakku hat Vorrang bis 80 %: was er aufnimmt, bekäme ein Fahrzeug nicht
    p.connected = false;
    p.mode = ChargeMode::Smart;
    house(p, 7300, 2200, -2300, -200);
    p.batterySoc = 20;
    p.prioritySoc = 80;
  } else if (!strcmp(name, "nightfast")) {
    // Nachts schnell laden, der Hausakku hilft mit (z. B. über den evcc-Optimizer)
    p.mode = ChargeMode::Fast;
    p.charging = true;
    p.chargePower = 10300;
    p.phasesActive = 3;
    p.greenShare = 0.32f;
    house(p, 0, 1400, 4700, 7000);
    p.batterySoc = 55;
    p.chargedEnergy = 5200;
    p.chargeDuration = 1800;
    p.sessionSolarPct = 30;
    p.solarToday = 0;
  } else if (!strcmp(name, "waiting")) {
    p.mode = ChargeMode::Smart;
  } else if (!strcmp(name, "limit")) {
    p.mode = ChargeMode::Smart;
    p.vehicleSoc = 80;
    p.limitSoc = 80;
  } else if (!strcmp(name, "boostready")) {
    p.mode = ChargeMode::Smart;
  } else if (!strcmp(name, "boost")) {
    p.mode = ChargeMode::Smart;
    p.batteryBoost = true;
    p.charging = true;
    p.chargePower = 7360;
    p.phasesActive = 3;
    p.greenShare = 0.88f;  // evcc zählt den Hausakku zum eigenen Strom
    p.chargedEnergy = 3100;
    p.chargeDuration = 1260;
    p.sessionSolarPct = 84;
    house(p, 2600, 900, 4800, 860);
    p.batterySoc = 64;
  } else if (!strcmp(name, "start")) {
    p.mode = ChargeMode::Smart;
    strncpy(p.pvAction, "enable", sizeof(p.pvAction) - 1);
    p.pvRemaining = 45;
  } else if (!strcmp(name, "heating") || !strcmp(name, "heatingdone")) {
    // Wärmepumpe für Warmwasser: evcc meldet die Temperatur als Ladestand
    strncpy(p.title, "Wärmepumpe", sizeof(p.title) - 1);
    p.vehicleTitle[0] = 0;
    p.heating = true;
    p.mode = ChargeMode::Smart;
    p.vehicleSoc = 43.3f;
    p.limitSoc = 55;
    p.chargedEnergy = 1800;
    p.chargeDuration = 2700;
    p.sessionSolarPct = 91;
    if (!strcmp(name, "heating")) {
      p.charging = true;
      p.chargePower = 2100;
      p.phasesActive = 1;
      p.greenShare = 1.0f;
      house(p, 3400, 900, -400, 0);
    } else {
      p.vehicleSoc = 55.2f;
    }
  } else if (!strcmp(name, "off")) {
    p.mode = ChargeMode::None;
    p.modeOff = true;
  } else {
    return false;
  }
  return true;
}
