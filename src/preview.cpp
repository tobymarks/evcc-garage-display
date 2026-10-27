#include "preview.h"

#include <string.h>

const char* const preview::NAMES = "charging|solar|fast|nocar|nocarlow|night|waiting|start|off|stale";

// Typisches Haus am Mittag: PV 6,2 kW, Haus 0,9 kW, Akku 78 %; Smart braucht einphasig 1,4 kW
static void house(EvccState& p, float pv, float home) {
  p.pvPower = pv;
  p.homePower = home;
  p.batterySoc = 78;
  p.minChargePower = 6 * 230;
}

bool preview::scene(const char* name, EvccState& p, bool& stale) {
  p = EvccState();
  p.valid = true;
  strncpy(p.title, "Garage", sizeof(p.title) - 1);
  p.connected = true;
  p.mode = ChargeMode::Slow;
  strncpy(p.vehicleTitle, "ID.4", sizeof(p.vehicleTitle) - 1);
  p.vehicleSoc = 41;
  stale = false;
  house(p, 6200, 900);

  if (!strcmp(name, "charging") || !strcmp(name, "stale")) {
    p.charging = true;
    p.chargePower = 7360;
    p.phasesActive = 3;
    p.greenShare = 0.62f;
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
    p.chargedEnergy = 2900;
    p.chargeDuration = 1680;
    p.sessionSolarPct = 97;
  } else if (!strcmp(name, "fast")) {
    p.mode = ChargeMode::Fast;
    p.charging = true;
    p.chargePower = 11040;
    p.phasesActive = 3;
    p.greenShare = 0.04f;
    p.chargedEnergy = 7400;
    p.chargeDuration = 2460;
    p.sessionSolarPct = 6;
  } else if (!strcmp(name, "nocar")) {
    p.connected = false;
  } else if (!strcmp(name, "nocarlow")) {
    p.connected = false;
    p.mode = ChargeMode::Smart;
    house(p, 1900, 1100);
  } else if (!strcmp(name, "night")) {
    p.connected = false;
    house(p, 0, 450);
  } else if (!strcmp(name, "waiting")) {
    p.mode = ChargeMode::Smart;
  } else if (!strcmp(name, "start")) {
    p.mode = ChargeMode::Smart;
    strncpy(p.pvAction, "enable", sizeof(p.pvAction) - 1);
    p.pvRemaining = 45;
  } else if (!strcmp(name, "off")) {
    p.mode = ChargeMode::None;
    p.modeOff = true;
  } else {
    return false;
  }
  return true;
}
