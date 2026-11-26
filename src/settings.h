#pragma once

#include <Arduino.h>

#include "config.h"
#include "pv_scaling.h"

// Ladepunktnummern liegen in 1…99. 0 folgt dem ersten Eintrag der Ladepunktliste.
constexpr uint8_t START_SOLAR = 100;
constexpr uint8_t SOLAR_LAST = MAX_LOADPOINTS;

struct Settings {
  String evccHost;
  uint16_t evccPort;
  uint8_t loadpoints[MAX_LOADPOINTS];  // Nummern 1-basiert, wie in der evcc-API
  uint8_t loadpointCount;              // ≥ 1; bei mehreren wird zwischen ihnen gewischt
  float latitude;
  float longitude;
  uint8_t rotation;   // 0–3 (× 90°)
  uint8_t theme;      // 0 = automatisch, 1 = hell, 2 = dunkel
  uint8_t screenTimeoutMin;  // 0 = Bildschirm immer an
  bool autoUpdate;           // neue Firmware selbst installieren
  bool boostButton;          // Akku-Anzeige oben schaltet im Modus Smart Battery Boost
  uint8_t brightnessPct;     // Helligkeit 10–100 %
  bool vehicleSwitch;        // Fahrzeugzeile antippen wechselt das Fahrzeug
  bool chargeLimit;          // Ladelimit neben der Fahrzeugzeile anzeigen und per Antippen einstellen
  uint8_t loadpointTile[3];  // Kacheln bei angestecktem Fahrzeug, von links nach rechts
  bool solarPage;            // zusätzliche Seite für Erzeugung, Tagesverlauf und Prognose
  bool solarSurplus;         // alte Einstellung (vor dem Editor), nur noch Startwert für solarBig
  uint8_t solarBig;          // Solarseite, große Zahl: SOLAR_BIG_…
  uint8_t solarMid;          // Solarseite, Mitte: SOLAR_MID_…
  uint8_t solarTile[3];      // Solarseite, Kacheln von links: SOLAR_TILE_…
  pv::Peaks solarPeaks = {}; // optionale Nennleistung je evcc-Zähler
  uint8_t startPage;         // 0, Ladepunktnummer oder START_SOLAR; unabhängig von der Wischreihenfolge
  uint8_t solarPosition;     // Anzahl Ladepunkte vor der Solarseite, SOLAR_LAST = immer am Ende
  uint32_t backlightHz;      // Taktung der Hintergrundbeleuchtung ab dem Start: Schalter „Dimmen mit langsamer Taktung“ oder /backlight?hz=…&save=1 (#21)
  uint8_t language;          // Sprache der Oberfläche, siehe Lang in lang.h; ein Wechsel wirkt nach dem Neustart
  String hostname;           // Name im Heimnetz: <name>.local, Geräteliste des Routers, OTA (#47)
  String password;           // schützt Weboberfläche und OTA (Benutzer "admin"); leer = offen
};

namespace settings {
void load();
// "1, 3" → {1, 3}; ungültige und doppelte Nummern fallen weg, leer → Ladepunkt 1
void parseLoadpoints(const char* text, Settings& s);
// "Küche 2" → "kueche-2": nur a–z, 0–9 und Bindestrich, höchstens MAX_HOSTNAME_LEN Zeichen; leer → ""
String cleanHostname(const String& text);
// {1, 3} → "1,3"
String loadpointsText(const Settings& s);
void save();
Settings& get();
// Helligkeit als PWM-Wert 0–255
uint8_t backlight();
}  // namespace settings
