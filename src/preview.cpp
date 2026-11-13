#include "preview.h"

#include <math.h>
#include <string.h>

const char* const preview::NAMES = "charging|solar|fast|nocar|nocarlow|nocarfull|night|evening|priority|nightfast|waiting|limit|boostready|boost|start|off|stale|heating|heatingdone|limitsoc|limitkwh|pv|pvmulti|pvnight|pvsummer|pvold";

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
  p.socBased = true;
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
  } else if (!strcmp(name, "limitsoc") || !strcmp(name, "limitkwh")) {
    // Ladelimit einstellbar: mit Ladestand in Prozent, ohne (Fahrzeug ohne Schnittstelle) als Energie
    p.mode = ChargeMode::Smart;
    p.charging = true;
    p.chargePower = 4140;
    p.phasesActive = 1;
    p.greenShare = 1.0f;
    house(p, 6200, 900, -1160, 0);
    p.chargedEnergy = 12400;
    p.chargeDuration = 10800;
    p.sessionSolarPct = 97;
    p.vehicleSoc = 64;
    p.limitSoc = 80;
    if (!strcmp(name, "limitkwh")) {
      strncpy(p.vehicleTitle, "ID. Buzz", sizeof(p.vehicleTitle) - 1);
      p.vehicleSoc = 0;
      p.limitSoc = 100;
      p.socBased = false;
      p.limitEnergy = 30;
    }
  } else if (!strcmp(name, "off")) {
    p.mode = ChargeMode::None;
    p.modeOff = true;
  } else {
    return false;
  }
  return true;
}

// Ein Novembertag mit einer 10-kWp-Anlage: erzeugte kWh je Stunde und die Prognose dazu
static const float PV_MADE[24] = {0, 0, 0, 0, 0, 0, 0, 0.01f, 1.02f, 3.19f, 4.11f, 4.46f, 4.41f, 2.5f, 0.51f, 1.03f, 0.08f};
static const float PV_FORECAST[24] = {0,     0,     0,     0,     0,     0,     0.07f, 0.42f, 1.07f, 1.95f,
                                      3.02f, 3.05f, 2.65f, 2.29f, 1.72f, 0.92f, 0.25f, 0.05f, 0.01f};

bool preview::solarScene(const char* name, EvccState& p, EvccSolar& solar, int8_t& hour) {
  p = EvccState();
  p.valid = true;
  strncpy(p.title, "Garage", sizeof(p.title) - 1);
  p.mode = ChargeMode::Slow;
  house(p, 4400, 900, -3200, -300);
  p.solarToday = 4300;
  p.solarTomorrow = 19000;
  solar = EvccSolar();
  solar.valid = true;
  solar.plantTotal = solar.plantCount = 1;
  strncpy(solar.plantTitle[0], "Photovoltaik", sizeof(solar.plantTitle[0]) - 1);
  solar.plantPower[0] = 4400;
  hour = 13;
  memcpy(solar.hourForecast, PV_FORECAST, sizeof(PV_FORECAST));
  for (int h = 0; h < 13; h++) solar.hourEnergy[h] = PV_MADE[h];
  solar.hourEnergy[13] = 1.0f;  // die laufende Stunde ist erst zum Teil vorbei

  if (!strcmp(name, "pv")) {
  } else if (!strcmp(name, "pvmulti")) {
    static const char* const TITLES[3] = {"Dach Süd", "Dach West", "Garage"};
    static const float POWER[3] = {2600, 1300, 500};
    solar.plantTotal = solar.plantCount = 3;
    for (int i = 0; i < 3; i++) {
      strncpy(solar.plantTitle[i], TITLES[i], sizeof(solar.plantTitle[0]) - 1);
      solar.plantPower[i] = POWER[i];
    }
  } else if (!strcmp(name, "pvnight")) {
    hour = 21;
    memcpy(solar.hourEnergy, PV_MADE, sizeof(PV_MADE));
    house(p, 0, 450, 350, 100);
    p.batterySoc = 64;
    p.solarToday = 0;
    p.solarTomorrow = 19000;
    solar.plantPower[0] = 0;
  } else if (!strcmp(name, "pvsummer")) {
    // Langer Sommertag mit großer Anlage: mehr Stunden, dreistellige Tageswerte
    hour = 15;
    for (int h = 5; h <= 21; h++) {
      const float x = (h - 13) / 4.6f;
      solar.hourForecast[h] = 22.0f * expf(-x * x);
      solar.hourEnergy[h] = h < 15 ? solar.hourForecast[h] * (h % 4 == 0 ? 0.8f : 1.05f) : 0;
    }
    solar.hourEnergy[15] = 6.5f;
    house(p, 18600, 1400, -5000, -12200);
    p.solarToday = 48200;
    p.solarTomorrow = 152000;
    solar.plantPower[0] = 18600;
  } else if (!strcmp(name, "pvold")) {
    // evcc ohne Verlauf: keine Stundenwerte, kein Tagesertrag
    solar.valid = false;
  } else {
    return false;
  }
  for (int h = 0; h < 24; h++) {
    solar.today += solar.hourEnergy[h];
    solar.forecastToday += solar.hourForecast[h];
  }
  return true;
}
