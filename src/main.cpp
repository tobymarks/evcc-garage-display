// evcc-Garagendisplay: zeigt die Ladeleistung eines evcc-Ladepunkts und schaltet
// zwischen PV, Smart und Schnell um. Hardware: Guition ESP32-4848S040C (4", 480x480).

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_core_dump.h>
#include <esp_flash.h>
#include <esp_flash_partitions.h>
#include <esp_heap_caps.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_rom_crc.h>
#include <esp_rom_sys.h>
#include <rom/rtc.h>
#include <functional>
#include <lvgl.h>

#include "config.h"
#include "display.h"
#include "evcc_client.h"
#include "fw_version.h"
#include "icons.h"
#include "preview.h"
#include "settings.h"
#include "sun.h"
#include "ui.h"
#include "updater.h"

static WiFiManager wm;
static WiFiManagerParameter* pHost;
static WiFiManagerParameter* pPort;
static WiFiManagerParameter* pLoadpoint;
static WiFiManagerParameter* pLat;
static WiFiManagerParameter* pLon;
static WiFiManagerParameter* pRotate;
static WiFiManagerParameter* pTheme;
static WiFiManagerParameter* pTimeout;
static WiFiManagerParameter* pAutoUpdate;
static WebServer web(8080);  // Port 80 gehört dem Einstellungsportal

static bool portalActive = false;
static bool otaStarted = false;
static bool timeStarted = false;
static bool webStarted = false;
static uint32_t restartAt = 0;

static EvccState states[MAX_LOADPOINTS];
static uint8_t current = 0;  // angezeigter Ladepunkt (Position in den Einstellungen)
static EvccVehicles vehicles;  // in evcc angelegte Fahrzeuge
static EvccSolar solar;          // Stundenwerte und Anlagen für die Solarseite
static bool solarShown = false;  // Solarseite (hinter oder vor den Ladepunkten) statt eines Ladepunkts
// Ist die Solarseite die Startseite, kehrt das Display so lange nach der letzten Berührung zu ihr zurück
static constexpr uint32_t SOLAR_RETURN_MS = 120000;
// Angefragte Änderung (Modus, Boost, Fahrzeug), bis evcc sie bestätigt
struct Pending {
  bool active = false;
  uint32_t id = 0;     // Nummer der Anfrage im evcc-Client; 0 = noch nicht abgeschickt
  uint32_t since = 0;
};
static Pending pendings[(int)Command::Count][MAX_LOADPOINTS];
static ChargeMode modeTarget[MAX_LOADPOINTS];
static bool boostTarget[MAX_LOADPOINTS];
static char vehicleTargetName[MAX_LOADPOINTS][VEHICLE_NAME_LEN];
static char vehicleTargetTitle[MAX_LOADPOINTS][32];
static float limitTarget[MAX_LOADPOINTS];
static bool limitTargetEnergy[MAX_LOADPOINTS];  // Limit in kWh statt in Prozent
static uint32_t vehicleSendAt[MAX_LOADPOINTS];  // erst abschicken, wenn nicht weitergetippt wird
static const char* flashMsg = nullptr;
static uint32_t flashUntil = 0;
static bool dirty = true;

static uint32_t lastActivity = 0;
static int brightness = 0;
// Bildschirm über /sleep ausgeschaltet; gilt, bis sich lastActivity wieder ändert
static bool forcedSleep = false;
static uint32_t forcedSleepActivity = 0;

// Vorschau einzelner Zustände über http://evcc-garage.local/preview?s=…
static EvccState previewState;
static bool previewStale = false;
static int previewDark = -1;  // -1 = Einstellung verwenden
static uint32_t previewUntil = 0;
static bool previewLimit = false;  // Ladelimit in der Vorschau zeigen, auch wenn es ausgeschaltet ist
static bool previewBoost = false;  // Boost-Knopf in der Vorschau zeigen, auch wenn er ausgeschaltet ist
static bool previewSolar = false;  // Vorschau der Solarseite …
static bool previewLive = false;   // … mit den echten Daten statt eines Beispielzustands (?s=pvlive)
static EvccSolar previewSolarData;
static int8_t previewHour = -1;
static int8_t previewSurplus = -1;  // Solarseite in der Vorschau mit (1) oder ohne (0) Überschuss, -1 = Einstellung

// ---------- WLAN-Einrichtung / Einstellungen ----------

// Übernimmt die Werte aus dem Portal oder von /settings (gleiche Feldnamen).
// Liefert true, wenn die Änderung erst nach einem Neustart wirkt (Verbindung, Drehung, Passwort);
// alles andere liest die Oberfläche laufend aus den Einstellungen.
static bool applySettings(std::function<String(const char*)> value) {
  Settings& s = settings::get();
  const String oldHost = s.evccHost, oldLps = settings::loadpointsText(s), oldPassword = s.password;
  const uint16_t oldPort = s.evccPort;
  const uint8_t oldRotation = s.rotation;
  s.evccHost = value("host");
  s.evccHost.trim();
  // Nur der Rechnername zählt: "http://" davor und einen Pfad dahinter verzeihen
  const int scheme = s.evccHost.indexOf("://");
  if (scheme >= 0) s.evccHost.remove(0, scheme + 3);
  const int slash = s.evccHost.indexOf('/');
  if (slash >= 0) s.evccHost.remove(slash);
  s.evccPort = constrain(value("port").toInt(), 1, 65535);
  settings::parseLoadpoints(value("lp").c_str(), s);
  s.latitude = value("lat").toFloat();
  s.longitude = value("lon").toFloat();
  s.rotation = constrain(value("rot").toInt(), 0, 3);
  s.theme = constrain(value("theme").toInt(), 0, 2);
  s.screenTimeoutMin = constrain(value("timeout").toInt(), 0, 240);
  s.autoUpdate = value("autoupd").toInt() != 0;
  // Fehlt im WLAN-Portal – dort bleibt die bisherige Einstellung
  const String boost = value("boost");
  if (!boost.isEmpty()) s.boostButton = boost.toInt() != 0;
  const String bright = value("bright");
  if (!bright.isEmpty()) s.brightnessPct = constrain(bright.toInt(), MIN_BRIGHTNESS_PCT, 100);
  const String chargeLimit = value("limit");
  if (!chargeLimit.isEmpty()) s.chargeLimit = chargeLimit.toInt() != 0;
  const String vehicleSwitch = value("vehsw");
  if (!vehicleSwitch.isEmpty()) s.vehicleSwitch = vehicleSwitch.toInt() != 0;
  const String solarPage = value("solar");
  if (!solarPage.isEmpty()) s.solarPage = solarPage.toInt() != 0;
  const String solarSurplus = value("solarsur");
  if (!solarSurplus.isEmpty()) s.solarSurplus = solarSurplus.toInt() != 0;
  const String solarFirst = value("solar1st");
  if (!solarFirst.isEmpty()) s.solarFirst = solarFirst.toInt() != 0;
  // Leeres Feld = Passwort unverändert; entfernt wird es nur ausdrücklich
  const String password = value("pw");
  if (value("pwclear").toInt() != 0)
    s.password = "";
  else if (!password.isEmpty())
    s.password = password.substring(0, MAX_PASSWORD_LEN);
  settings::save();
  return s.evccHost != oldHost || s.evccPort != oldPort || settings::loadpointsText(s) != oldLps ||
         s.rotation != oldRotation || s.password != oldPassword;
}

static void saveParams() {
  // Wer am Display das Portal öffnet, steht davor: so lässt sich ein vergessenes Passwort löschen
  settings::get().password = "";
  updater::confirm();  // wer das Portal bedient, hat ein laufendes Display vor sich
  restartAt = millis() + 2000;  // Antwortseite noch ausliefern, dann neu starten
  applySettings([](const char* key) {
    for (int i = 0; i < wm.getParametersCount(); i++) {
      WiFiManagerParameter* p = wm.getParameters()[i];
      if (p->getID() && !strcmp(p->getID(), key)) return String(p->getValue());
    }
    return String();
  });
}

static void setupPortal() {
  const Settings& s = settings::get();
  static char port[6], lp[24], lat[12], lon[12], theme[2], rot[2], timeout[4], autoUpdate[2];
  snprintf(port, sizeof(port), "%u", s.evccPort);
  strlcpy(lp, settings::loadpointsText(s).c_str(), sizeof(lp));
  snprintf(lat, sizeof(lat), "%.4f", s.latitude);
  snprintf(lon, sizeof(lon), "%.4f", s.longitude);
  snprintf(theme, sizeof(theme), "%u", s.theme);
  snprintf(rot, sizeof(rot), "%u", s.rotation);
  snprintf(timeout, sizeof(timeout), "%u", s.screenTimeoutMin);
  snprintf(autoUpdate, sizeof(autoUpdate), "%u", s.autoUpdate);

  pHost = new WiFiManagerParameter("host", "evcc-Adresse", s.evccHost.c_str(), 40);
  pPort = new WiFiManagerParameter("port", "evcc-Port", port, 5);
  pLoadpoint = new WiFiManagerParameter(
      "lp", "Ladepunkt-Nr. (1 = erster in evcc). Mehrere mit Komma, z. B. 1,2,3 – dann wischen zum Wechseln", lp, 17);
  pLat = new WiFiManagerParameter("lat", "Breitengrad (für Tag/Nacht)", lat, 11);
  pLon = new WiFiManagerParameter("lon", "Längengrad (für Tag/Nacht)", lon, 11);
  pRotate = new WiFiManagerParameter("rot", "Drehung: 0–3 (je 90° im Uhrzeigersinn)", rot, 1);
  pTheme = new WiFiManagerParameter("theme", "Design: 0 = automatisch (Sonnenstand), 1 = immer hell, 2 = immer dunkel", theme, 1);
  pTimeout = new WiFiManagerParameter("timeout", "Bildschirm aus nach Minuten ohne Ladung und Bedienung (0 = immer an)", timeout, 3);
  pAutoUpdate = new WiFiManagerParameter("autoupd", "Automatische Updates: 1 = an, 0 = aus", autoUpdate, 1);
  static WiFiManagerParameter version("<p>Firmware " FW_VERSION "</p>");
  for (auto* p : {pHost, pPort, pLoadpoint, pLat, pLon, pRotate, pTheme, pTimeout, pAutoUpdate, &version})
    wm.addParameter(p);

  wm.setTitle("evcc-Garage");
  wm.setHostname(HOSTNAME);
  static std::vector<const char*> menu = {"wifi", "param", "info", "restart"};
  wm.setMenu(menu);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(20);
  wm.setSaveParamsCallback(saveParams);
}

static void showPortalMessage() {
  ui::showMessage(ICON_SETTINGS, "Einstellungen",
                  "Mit dem WLAN \"" PORTAL_AP_NAME "\" verbinden. Die Seite öffnet sich automatisch, "
                  "sonst 192.168.4.1 aufrufen. Antippen zum Abbrechen.");
}

static void openSettings() {
  if (portalActive) return;
  wm.startConfigPortal(PORTAL_AP_NAME);
  portalActive = true;
  showPortalMessage();
}

static void closeSettings() {
  if (!portalActive || WiFi.status() != WL_CONNECTED) return;  // ohne WLAN bleibt das Portal offen
  wm.stopConfigPortal();
}

// ---------- Bedienung ----------

static bool previewActive();

static Pending& pending(Command command, uint8_t index) { return pendings[(int)command][index]; }

static void startPending(Command command, uint8_t index, uint32_t id) {
  pending(command, index) = {true, id, millis()};
  flashMsg = nullptr;
  dirty = true;
}

static void onModeSelected(ChargeMode mode) {
  lastActivity = millis();
  if (previewActive()) return;  // die Vorschau zeigt nicht den echten Ladepunkt
  const ChargeMode shown = pending(Command::Mode, current).active ? modeTarget[current] : states[current].mode;
  if (mode == shown) return;
  modeTarget[current] = mode;
  startPending(Command::Mode, current, evcc::requestMode(current, mode));
}

// Antippen der Akku-Anzeige: Battery Boost umschalten
static void onBoostTapped() {
  lastActivity = millis();
  if (previewActive()) return;
  const bool shown = pending(Command::Boost, current).active ? boostTarget[current] : states[current].batteryBoost;
  boostTarget[current] = !shown;
  startPending(Command::Boost, current, evcc::requestBoost(current, !shown));
}

// Antippen der Fahrzeugzeile: durch die in evcc angelegten Fahrzeuge und "Gastfahrzeug" wechseln.
// Abgeschickt wird erst, wenn nicht weitergetippt wird – sonst bekäme der Ladepunkt beim
// Durchtippen kurz jedes Fahrzeug dazwischen zugewiesen.
static void onVehicleTapped() {
  lastActivity = millis();
  if (previewActive()) return;
  const EvccState& st = states[current];
  if (!settings::get().vehicleSwitch || !st.valid || !st.connected || st.heating) return;
  const bool wasPending = pending(Command::Vehicle, current).active;
  const char* shown = wasPending ? vehicleTargetName[current] : st.vehicleName;
  if (vehicles.count == 0 && !shown[0]) return;

  int idx = -1;
  for (uint8_t i = 0; i < vehicles.count; i++) {
    if (!strcmp(vehicles.names[i], shown)) {
      idx = i;
      break;
    }
  }
  const int next = idx < 0 ? 0 : idx + 1;
  // Zeiger in die Liste, nicht in das Ziel selbst – das wird gleich überschrieben
  const char* targetName = next < vehicles.count ? vehicles.names[next] : "";
  const char* targetTitle = next < vehicles.count ? vehicles.titles[next] : "Gastfahrzeug";
  strlcpy(vehicleTargetName[current], targetName, sizeof(vehicleTargetName[0]));
  strlcpy(vehicleTargetTitle[current], targetTitle, sizeof(vehicleTargetTitle[0]));
  startPending(Command::Vehicle, current, 0);
  vehicleSendAt[current] = millis() + VEHICLE_TAP_DELAY_MS;
}

// Ladelimit in der Auswahl gewählt
static void onLimitSelected(bool energy, float value) {
  lastActivity = millis();
  if (previewActive()) return;
  const EvccState& st = states[current];
  if (!settings::get().chargeLimit || !st.valid || !st.connected || st.heating) return;
  limitTarget[current] = value;
  limitTargetEnergy[current] = energy;
  startPending(Command::Limit, current, evcc::requestLimit(current, energy, value));
}

static void showLoadpoint(uint8_t index) {
  if (index >= evcc::count() || (index == current && !solarShown)) return;
  current = index;
  solarShown = false;
  flashMsg = nullptr;  // Fehlermeldungen gehören zum vorher gezeigten Ladepunkt
  dirty = true;
}

// Die Solarseite liegt hinter den Ladepunkten oder – Einstellung "Solarseite zuerst" – davor
static bool solarFirst() { return settings::get().solarPage && settings::get().solarFirst; }

// Wischen nach links → nächste Seite: die Ladepunkte, davor oder dahinter die Solarseite (Einstellung)
static void onSwipe(int delta) {
  lastActivity = millis();
  const int first = solarFirst() ? 1 : 0;  // Seite des ersten Ladepunkts
  const int solarPos = solarFirst() ? 0 : evcc::count();
  const int next = (solarShown ? solarPos : current + first) + delta;
  if (next == solarPos && settings::get().solarPage) {
    solarShown = true;
    flashMsg = nullptr;
    dirty = true;
  } else if (next >= first) {
    showLoadpoint(next - first);
  }
}

static bool anyCharging() {
  for (uint8_t i = 0; i < evcc::count(); i++)
    // Fällt evcc beim Laden aus, bleibt der letzte Stand stehen – nach einer Weile zählt er nicht mehr
    if (states[i].valid && states[i].charging && millis() - states[i].updatedMs < CHARGING_STALE_MS) return true;
  return false;
}

static bool anyPending() {
  for (uint8_t i = 0; i < evcc::count(); i++)
    for (int c = 0; c < (int)Command::Count; c++)
      if (pendings[c][i].active) return true;
  return false;
}

static void flash(const char* msg) {
  flashMsg = msg;
  flashUntil = millis() + 5000;
  lastActivity = millis();
  dirty = true;
}

// ---------- Zustand aktualisieren ----------

static bool timeValid() { return time(nullptr) > 1700000000; }

static bool previewActive() { return previewUntil && (int32_t)(previewUntil - millis()) > 0; }

static bool darkTheme() {
  if (previewActive() && previewDark >= 0) return previewDark;
  const Settings& s = settings::get();
  if (s.theme == 1) return false;
  if (s.theme == 2) return true;
  return timeValid() && sun::elevation(time(nullptr), s.latitude, s.longitude) < NIGHT_SUN_ELEVATION;
}

// Prüft eine angefragte Änderung: reached = evcc meldet den gewünschten Wert.
// Liefert eine Fehlermeldung, wenn der Befehl scheiterte oder evcc ihn nicht übernommen hat.
static const char* checkPending(Command command, uint8_t i, bool reached, const char* failed, const char* refused) {
  Pending& p = pending(command, i);
  if (!p.active || !p.id) return nullptr;
  // Nur das Ergebnis genau dieser Anfrage zählt, nicht das einer früheren
  const CommandStatus done = evcc::status(command, i);
  if (done.id == p.id) {
    if (done.result == CommandResult::Failed) {
      p.active = false;
      return failed;
    }
    // Bestätigt erst ein Zustand, dessen Abfrage nach dem Befehl begonnen hat
    if (states[i].valid && (int32_t)(states[i].polledMs - done.doneMs) >= 0 && reached) {
      p.active = false;
      dirty = true;
      return nullptr;
    }
  }
  if (millis() - p.since > MODE_CONFIRM_TIMEOUT_MS) {
    p.active = false;
    return refused;
  }
  return nullptr;
}

static void updateState() {
  static bool lastConnected[MAX_LOADPOINTS], lastCharging[MAX_LOADPOINTS];
  static bool lastWifi = false, lastStale = true;
  static char lastClock[6] = "";

  if (evcc::latest(states, vehicles, solar)) dirty = true;
  if (solarShown && !settings::get().solarPage) {  // in den Einstellungen ausgeschaltet
    solarShown = false;
    dirty = true;
  }
  if (solarFirst() && !solarShown && millis() - display::lastTouchMs() > SOLAR_RETURN_MS) {
    solarShown = true;  // zurück zur Startseite
    flashMsg = nullptr;
    dirty = true;
  }

  const uint32_t touched = display::lastTouchMs();
  const bool inUse = touched && millis() - touched < AUTO_SWITCH_AFTER_TOUCH_MS;
  int jumpTo = -1;  // Ladepunkt, an dem sich gerade etwas getan hat
  for (uint8_t i = 0; i < evcc::count(); i++) {
    const EvccState& st = states[i];

    Pending& veh = pending(Command::Vehicle, i);
    if (veh.active && st.valid && !st.connected) {
      veh.active = false;  // abgesteckt: der Wechsel hat sich erledigt
      dirty = true;
    } else if (veh.active && !veh.id && (int32_t)(millis() - vehicleSendAt[i]) >= 0) {
      if (!strcmp(st.vehicleName, vehicleTargetName[i])) {
        veh.active = false;  // einmal im Kreis getippt – nichts zu tun
        dirty = true;
      } else {
        veh.id = evcc::requestVehicle(i, vehicleTargetName[i]);
        veh.since = millis();
      }
    }

    // Fehler nur melden, wenn der Ladepunkt gerade zu sehen ist
    const char* error = nullptr;
    const char* e;
    if ((e = checkPending(Command::Mode, i, st.mode == modeTarget[i], "Modus konnte nicht gesetzt werden",
                          "evcc hat den Modus nicht übernommen")))
      error = e;
    if ((e = checkPending(Command::Boost, i, st.batteryBoost == boostTarget[i],
                          "Boost konnte nicht geschaltet werden", "evcc hat den Boost nicht übernommen")))
      error = e;
    if ((e = checkPending(Command::Vehicle, i, !strcmp(st.vehicleName, vehicleTargetName[i]),
                          "Fahrzeug konnte nicht gewechselt werden", "evcc hat das Fahrzeug nicht übernommen")))
      error = e;
    Pending& lim = pending(Command::Limit, i);
    if (lim.active && st.valid && !st.connected) {
      lim.active = false;  // abgesteckt: evcc setzt das Limit ohnehin zurück
      dirty = true;
    }
    const bool limitReached = limitTargetEnergy[i] ? fabsf(st.limitEnergy - limitTarget[i]) < 0.05f
                                                   : lroundf(st.limitSoc) == lroundf(limitTarget[i]);
    if ((e = checkPending(Command::Limit, i, limitReached, "Ladelimit konnte nicht gesetzt werden",
                          "evcc hat das Ladelimit nicht übernommen")))
      error = e;
    if (error) {
      if (i == current) flash(error);
      dirty = true;
    }

    // Bildschirm wecken, wenn ein Auto angesteckt wird oder das Laden beginnt/endet
    if (st.connected != lastConnected[i] || st.charging != lastCharging[i]) {
      // Diesen Ladepunkt zeigen – außer beim Abstecken; ein ladender hat Vorrang
      const bool interesting = (st.connected && !lastConnected[i]) || st.charging != lastCharging[i];
      if (interesting && (jumpTo < 0 || (st.charging && !states[jumpTo].charging))) jumpTo = i;
      lastConnected[i] = st.connected;
      lastCharging[i] = st.charging;
      lastActivity = millis();
    }
  }
  // Nicht wegspringen, solange jemand das Display bedient – und nicht von der Solarseite, wenn sie
  // die Startseite ist (das Display soll dann den Überblick zeigen, nicht das Auto)
  if (jumpTo >= 0 && !inUse && !(solarShown && solarFirst())) showLoadpoint(jumpTo);
  if (flashMsg && (int32_t)(millis() - flashUntil) > 0) {
    flashMsg = nullptr;
    dirty = true;
  }

  const EvccState& state = states[current];
  const bool wifi = WiFi.status() == WL_CONNECTED;
  const bool stale = state.updatedMs == 0 || millis() - state.updatedMs > STALE_AFTER_MS;
  if (wifi != lastWifi || stale != lastStale) {
    lastWifi = wifi;
    lastStale = stale;
    dirty = true;
  }

  char clock[6] = "";
  if (timeValid()) {
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    strftime(clock, sizeof(clock), "%H:%M", &t);
  }
  ui::setNight(darkTheme());
  if (strcmp(clock, lastClock)) {
    strlcpy(lastClock, clock, sizeof(lastClock));
    dirty = true;
  }

  if (previewUntil && !previewActive()) {
    previewUntil = 0;
    dirty = true;
  }

  if (dirty) {
    dirty = false;
    const bool preview = previewActive();
    const bool example = preview && !previewLive;  // Beispielzustand statt der echten Daten
    UiView v;
    v.state = example ? previewState : state;
    v.wifiConnected = example || wifi;
    v.stale = example ? previewStale : stale;
    v.solar = preview ? previewSolar : solarShown;
    v.solarData = example && previewSolar ? previewSolarData : solar;
    v.solarSurplus = preview && previewSurplus >= 0 ? previewSurplus : settings::get().solarSurplus;
    v.pendingMode = pending(Command::Mode, current).active ? modeTarget[current] : ChargeMode::None;
    v.boostButton = settings::get().boostButton || (preview && previewBoost);
    v.pendingBoost = pending(Command::Boost, current).active ? boostTarget[current] : -1;
    v.pendingVehicle = !preview && pending(Command::Vehicle, current).active;
    v.hasVehicles = vehicles.count > 0;
    v.vehicleSwitch = settings::get().vehicleSwitch;
    v.limitButton = settings::get().chargeLimit || (preview && previewLimit);
    v.pendingLimit = !preview && pending(Command::Limit, current).active ? limitTarget[current] : -1;
    strlcpy(v.pendingVehicleTitle, vehicleTargetTitle[current], sizeof(v.pendingVehicleTitle));
    // Die Solarseite ist die letzte oder die erste Seite; in der Vorschau auch, wenn sie ausgeschaltet ist
    v.page = solarFirst() ? (v.solar ? 0 : current + 1) : (v.solar ? evcc::count() : current);
    v.pageCount = evcc::count() + (settings::get().solarPage || v.solar ? 1 : 0);
    v.flash = flashMsg;
    strlcpy(v.clock, clock, sizeof(v.clock));
    if (example && previewSolar) {
      v.hour = previewHour;
    } else if (timeValid()) {
      const time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      v.hour = t.tm_hour;
    }
    if (state.valid && timeValid()) {
      const time_t at = time(nullptr) - (millis() - state.updatedMs) / 1000;
      struct tm t;
      localtime_r(&at, &t);
      strftime(v.lastUpdate, sizeof(v.lastUpdate), "%H:%M", &t);
    }
    ui::render(v);
  }
}

static void updateBacklight() {
  if (display::takeWakeRequest()) lastActivity = millis();
  if ((int32_t)(display::lastTouchMs() - lastActivity) > 0) lastActivity = display::lastTouchMs();

  // Nach /sleep bleibt der Bildschirm aus, auch beim Laden und bei "immer an" – bis ihn jemand antippt,
  // /wake kommt oder sich am Ladepunkt etwas tut (alles setzt lastActivity neu)
  if (forcedSleep && lastActivity != forcedSleepActivity) forcedSleep = false;
  const bool keepOn = portalActive ||
                      (!forcedSleep && (anyCharging() || settings::get().screenTimeoutMin == 0 ||
                                        millis() - lastActivity < settings::get().screenTimeoutMin * 60000UL));
  display::setSleeping(!keepOn);

  const int target = keepOn ? settings::backlight() : 0;
  if (brightness != target) {
    // weich überblenden
    brightness += constrain(target - brightness, -12, 12);
    display::setBrightness(brightness);
  }
}

// ---------- Netzwerkdienste ----------

// Beispielzustände zum Prüfen der Oberfläche ohne echtes Auto, siehe src/preview.cpp
static void handlePreview() {
  EvccState p;
  const String name = web.arg("s");
  // Solarseite: Beispielzustände "pv…" oder mit ?s=pvlive die echten Daten – auch wenn sie ausgeschaltet ist
  previewLive = name == "pvlive";
  previewSolar = previewLive || preview::solarScene(name.c_str(), p, previewSolarData, previewHour);
  if (previewSolar) previewStale = false;
  if (!previewSolar && !preview::scene(name.c_str(), p, previewStale)) {
    previewUntil = 0;
    dirty = true;
    web.send(200, "text/plain",
             String("Vorschau beendet. Zustaende: ?s=") + preview::NAMES + "|pvlive [&dark=0|1]\n");
    return;
  }
  p.updatedMs = millis();
  if (states[current].valid) strlcpy(p.title, states[current].title, sizeof(p.title));
  if (web.hasArg("title")) strlcpy(p.title, web.arg("title").c_str(), sizeof(p.title));  // z. B. lange Namen prüfen

  previewState = p;
  previewBoost = !strncmp(web.arg("s").c_str(), "boost", 5);
  previewLimit = web.arg("s") == "limitsoc" || web.arg("s") == "limitkwh";
  previewDark = web.hasArg("dark") ? web.arg("dark").toInt() != 0 : -1;
  previewSurplus = web.hasArg("surplus") ? web.arg("surplus").toInt() != 0 : -1;
  previewUntil = millis() + PREVIEW_DURATION_MS;
  lastActivity = millis();
  dirty = true;
  web.send(200, "text/plain", "Vorschau aktiv fuer 60 s\n");
}

static volatile uint32_t wifiDisconnects = 0;
static volatile uint8_t lastDisconnectReason = 0;

// Neustart als vollständiger System-Reset. ESP.restart() setzt nur die CPU zurück; Display-Ausgabe und
// PSRAM laufen dabei weiter, und der folgende Start blieb auf diesem Board öfter hängen, bis der
// Watchdog eingriff. Direkt nach einem Update holt der Bootloader dann die alte Firmware zurück.
static void restartNow() {
  display::setBrightness(0);
  Serial.flush();
  esp_rom_software_reset_system();
  for (;;) {}
}

// ---------- Startprotokoll ----------
// Die letzten Starts bleiben im Flash stehen. So lässt sich nachvollziehen, ob ein Update wirklich
// gestartet ist oder ob der Bootloader nach einem zweiten Neustart die alte Firmware zurückgeholt hat.

struct BootEntry {
  char version[28];
  uint8_t reason;     // esp_reset_reason()
  uint8_t romReason;  // Rohwert des Chips, unterscheidet die Watchdogs
  uint8_t slot;       // 0 = app0, 1 = app1
  int8_t otaState;    // esp_ota_img_states_t beim Start, -1 = unbekannt
};
static constexpr int BOOT_LOG_SIZE = 8;
static BootEntry bootLog[BOOT_LOG_SIZE];  // neuester zuerst
static uint32_t bootNumber = 0;

static const char* resetReasonName(uint8_t reason) {
  static const char* const REASONS[] = {"unbekannt", "Einschalten", "extern", "Software", "Absturz",
                                        "Interrupt-Watchdog", "Task-Watchdog", "Watchdog", "Deep Sleep",
                                        "Unterspannung", "SDIO"};
  return reason < sizeof(REASONS) / sizeof(*REASONS) ? REASONS[reason] : "anderer";
}

static const char* otaStateName(int8_t state) {
  static const char* const STATES[] = {"neu", "unbestätigt", "bestätigt", "ungültig", "abgebrochen"};
  return state >= 0 && state < (int)(sizeof(STATES) / sizeof(*STATES)) ? STATES[state] : "unbekannt";
}

// Zustand eines Firmware-Platzes aus den beiden Starteinträgen (otadata). esp_ota_get_state_partition()
// nimmt den ersten passenden Eintrag. Nach einem gescheiterten und einem geglückten Update in denselben
// Platz gibt es zwei, und der erste kann der überholte sein – der Bootloader richtet sich nach dem gültigen.
static int8_t otaState(const esp_partition_t* partition) {
  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!partition || !otadata) return -1;
  const uint32_t slot = partition->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN;
  int8_t best = -1;
  for (int i = 0; i < 2; i++) {
    esp_ota_select_entry_t entry;
    if (esp_partition_read(otadata, i * otadata->erase_size, &entry, sizeof(entry)) != ESP_OK) continue;
    if (entry.ota_seq == UINT32_MAX || entry.ota_state > ESP_OTA_IMG_ABORTED) continue;
    if (entry.crc != esp_rom_crc32_le(UINT32_MAX, (const uint8_t*)&entry.ota_seq, sizeof(entry.ota_seq))) continue;
    if ((entry.ota_seq - 1) % 2 != slot) continue;  // zwei Firmware-Plätze, siehe partitions.csv
    const bool failed = entry.ota_state == ESP_OTA_IMG_INVALID || entry.ota_state == ESP_OTA_IMG_ABORTED;
    if (best < 0 || !failed) best = (int8_t)entry.ota_state;
  }
  return best;
}

static void recordBoot() {
  Preferences prefs;
  prefs.begin("boot", false);
  if (prefs.getBytesLength("log") == sizeof(bootLog)) prefs.getBytes("log", bootLog, sizeof(bootLog));
  bootNumber = prefs.getULong("n", 0) + 1;
  memmove(bootLog + 1, bootLog, sizeof(bootLog) - sizeof(bootLog[0]));
  BootEntry& e = bootLog[0];
  e = BootEntry();
  strlcpy(e.version, FW_VERSION, sizeof(e.version));
  e.reason = esp_reset_reason();
  e.romReason = rtc_get_reset_reason(0);
  const esp_partition_t* running = esp_ota_get_running_partition();
  e.slot = running ? running->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0 : 0xFF;
  e.otaState = otaState(running);
  prefs.putBytes("log", bootLog, sizeof(bootLog));
  prefs.putULong("n", bootNumber);
  prefs.end();
}

// Gerät, Firmware-Plätze, letzte Starts und der letzte Absturz
static String diagDevice() {
  const esp_reset_reason_t reason = esp_reset_reason();
  char buf[160];
  String s;
  snprintf(buf, sizeof(buf), "Firmware %s\nLaufzeit %lu s\nNeustartgrund: %s\n", FW_VERSION, millis() / 1000,
           resetReasonName(reason));
  s += buf;
  snprintf(buf, sizeof(buf), "WLAN %d dBm, Trennungen %lu", WiFi.RSSI(), (unsigned long)wifiDisconnects);
  s += buf;
  if (lastDisconnectReason) {
    snprintf(buf, sizeof(buf), " (zuletzt Grund %u: %s)", lastDisconnectReason,
             WiFi.STA.disconnectReasonName((wifi_err_reason_t)lastDisconnectReason));
    s += buf;
  }
  s += "\n";
  // Hintergrundbeleuchtung als PWM-Wert: zeigt, ob /wake, /sleep und der Timeout gegriffen haben
  snprintf(buf, sizeof(buf), "Bildschirm %s, Helligkeit %d von 255, Taktung %lu Hz (gespeichert für den Start: %lu Hz)\n",
           brightness == 0 ? (forcedSleep ? "aus (per /sleep)" : "aus") : "an", brightness,
           (unsigned long)display::backlightHz(), (unsigned long)settings::get().backlightHz);
  s += buf;
  snprintf(buf, sizeof(buf), "Interner RAM frei %u, minimal %u, größter Block %u\nPSRAM frei %u\n",
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  s += buf;
  snprintf(buf, sizeof(buf), "evcc-Task: Stack frei mindestens %lu Byte\n", (unsigned long)evcc::stackFree());
  s += buf;
  // Hersteller und Größe des Flash-Bausteins: die Boards werden mit verschiedenen bestückt
  uint32_t flashId = 0, flashSize = 0;
  esp_flash_read_id(nullptr, &flashId);
  esp_flash_get_physical_size(nullptr, &flashSize);
  snprintf(buf, sizeof(buf), "Flash-Baustein: Kennung 0x%06lx, %lu MB\n", (unsigned long)flashId,
           (unsigned long)(flashSize >> 20));
  s += buf;
  // Startete eine neue Firmware nicht sauber, hat der Bootloader die vorherige wiederhergestellt. Läuft
  // inzwischen eine Firmware aus demselben Platz, ist der Eintrag überholt: Dort steht dann eine andere.
  esp_app_desc_t desc;
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
  if (invalid && invalid != running && esp_ota_get_partition_description(invalid, &desc) == ESP_OK) {
    snprintf(buf, sizeof(buf), "Zurückgerollt: Firmware in %s (gebaut %s %s) startete nicht sauber\n", invalid->label,
             desc.date, desc.time);
    s += buf;
  }
  if (running) {
    snprintf(buf, sizeof(buf), "Laufende Firmware in %s: %s\n", running->label, otaStateName(otaState(running)));
    s += buf;
  }
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (other) {
    snprintf(buf, sizeof(buf), "Anderer Firmware-Platz %s: %s", other->label, otaStateName(otaState(other)));
    s += buf;
    // Dieselbe Prüfung wie im Bootloader (Aufbau, Prüfsumme, SHA-256): Lehnt er eine Firmware ab, die
    // hier als startfähig gilt, liegt es nicht an ihrem Inhalt
    if (updater::working()) {
      s += ", wird vielleicht gerade beschrieben\n";  // dann schlüge die Prüfung nur deshalb fehl
    } else {
      const esp_partition_pos_t pos = {other->address, other->size};
      esp_image_metadata_t image;
      const esp_err_t err = esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &image);
      if (err == ESP_OK)
        snprintf(buf, sizeof(buf), ", Inhalt startfähig (%lu Byte)\n", (unsigned long)image.image_len);
      else
        snprintf(buf, sizeof(buf), ", Inhalt nicht startfähig (leer oder beschädigt, Fehler 0x%x)\n", err);
      s += buf;
    }
  }
  s += updater::failureNote();
  s += "Letzte Starts (neuester zuerst; Nr., Firmware, Platz, Zustand beim Start, Grund):\n";
  for (int i = 0; i < BOOT_LOG_SIZE && bootLog[i].version[0]; i++) {
    const BootEntry& e = bootLog[i];
    snprintf(buf, sizeof(buf), "  %lu  %s  app%u  %s  %s (Chip %u)\n", (unsigned long)(bootNumber - i), e.version,
             e.slot, otaStateName(e.otaState), resetReasonName(e.reason), e.romReason);
    s += buf;
  }
  esp_core_dump_summary_t dump;
  if (esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(&dump) == ESP_OK) {
    snprintf(buf, sizeof(buf), "Letzter Absturz: Task %s, PC 0x%08lx, Ursache %lu, Adresse 0x%08lx\nBacktrace:",
             dump.exc_task, dump.exc_pc, dump.ex_info.exc_cause, dump.ex_info.exc_vaddr);
    s += buf;
    for (uint32_t i = 0; i < dump.exc_bt_info.depth; i++) {
      snprintf(buf, sizeof(buf), " 0x%08lx", dump.exc_bt_info.bt[i]);
      s += buf;
    }
    s += dump.exc_bt_info.corrupted ? " (unvollständig)\n" : "\n";
  } else {
    s += "Kein Absturz gespeichert\n";
  }
  return s;
}

static const char* yesNo(bool value) { return value ? "ja" : "nein"; }

// Hardware, Einstellungen und was evcc liefert – ohne Passwörter, WLAN-Name, Adressen, Standort und
// ohne Namen von Ladepunkten und Fahrzeugen, damit die Diagnose öffentlich an ein Issue passt
static String diagDetails() {
  const Settings& st = settings::get();
  char buf[240];
  String s = "\nHardware\n";
  snprintf(buf, sizeof(buf), "Chip: %s Revision v%u.%u, %u Kerne, %lu MHz, %.0f °C\n", ESP.getChipModel(),
           (unsigned)ESP.getChipRevision() / 100, (unsigned)ESP.getChipRevision() % 100, (unsigned)ESP.getChipCores(),
           (unsigned long)getCpuFrequencyMhz(), temperatureRead());
  s += buf;
  snprintf(buf, sizeof(buf), "Speicher gesamt: interner RAM %u, PSRAM %u\n",
           heap_caps_get_total_size(MALLOC_CAP_INTERNAL), heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
  s += buf;
  s += "Touch: " + display::touchInfo() + "\n";
  snprintf(buf, sizeof(buf), "Gebaut: %s %s, ESP-IDF %s, Arduino %s\n", __DATE__, __TIME__, esp_get_idf_version(),
           ESP_ARDUINO_VERSION_STR);
  s += buf;

  s += "\nUpdates\n";
  const String update = updater::status();  // erste Zeile ist die Firmware, die steht schon oben
  s += update.substring(update.indexOf('\n') + 1);

  s += "\nEinstellungen\n";
  IPAddress ip;
  snprintf(buf, sizeof(buf), "evcc-Adresse als %s, Port %u, Ladepunkte %s\n",
           ip.fromString(st.evccHost) ? "IP-Adresse" : "Name", st.evccPort, settings::loadpointsText(st).c_str());
  s += buf;
  static const char* const THEMES[] = {"automatisch", "hell", "dunkel"};
  snprintf(buf, sizeof(buf), "Drehung %u°, Farbschema %s, Helligkeit %u %%, Bildschirm aus nach %u min%s\n",
           st.rotation * 90, THEMES[st.theme % 3], st.brightnessPct, st.screenTimeoutMin,
           st.screenTimeoutMin ? "" : " (immer an)");
  s += buf;
  snprintf(buf, sizeof(buf),
           "Battery Boost %s, Fahrzeugwechsel %s, Ladelimit %s, Solarseite %s (Überschuss %s, zuerst %s), "
           "Passwort gesetzt %s, Standort gesetzt %s\n",
           yesNo(st.boostButton), yesNo(st.vehicleSwitch), yesNo(st.chargeLimit), yesNo(st.solarPage),
           yesNo(st.solarSurplus), yesNo(st.solarFirst),
           yesNo(!st.password.isEmpty()), yesNo(st.latitude != 0 || st.longitude != 0));
  s += buf;
  snprintf(buf, sizeof(buf), "WLAN-Kanal %d, Uhrzeit bekannt %s\n", (int)WiFi.channel(), yesNo(timeValid()));
  s += buf;

  s += "\nevcc\n";
  const evcc::Diagnostics d = evcc::diagnostics();
  snprintf(buf, sizeof(buf), "Version %s\nAbfragen %lu, davon ohne brauchbare Antwort %lu; letzte: Code %d, %lu ms; längste %lu ms\n",
           d.version[0] ? d.version : "unbekannt", (unsigned long)d.polls, (unsigned long)d.failed, d.lastCode,
           (unsigned long)d.lastDurationMs, (unsigned long)d.maxDurationMs);
  s += buf;
  if (d.lastOkMs)
    snprintf(buf, sizeof(buf), "Letzte brauchbare Antwort vor %lu s\n", (unsigned long)((millis() - d.lastOkMs) / 1000));
  else
    snprintf(buf, sizeof(buf), "Noch keine brauchbare Antwort\n");
  s += buf;
  static const char* const MODES[] = {"keiner", "Smart", "Langsam", "Schnell"};
  for (uint8_t i = 0; i < evcc::count(); i++) {
    const EvccState& e = states[i];
    if (!e.valid) {
      snprintf(buf, sizeof(buf), "Ladepunkt %u: %s\n", st.loadpoints[i],
               e.missing ? "in evcc nicht vorhanden" : "noch keine Daten");
    } else {
      snprintf(buf, sizeof(buf),
               "Ladepunkt %u: Modus %s%s, angesteckt %s, lädt %s, %.0f W, Phasen %d, Heizgerät %s, Fahrzeug zugeordnet %s, "
               "Ladestand %.0f, Limit %.0f, Limit in Prozent %s\n",
               st.loadpoints[i], MODES[(uint8_t)e.mode % 4], e.modeOff ? " (aus)" : "", yesNo(e.connected),
               yesNo(e.charging), e.chargePower, e.phasesActive, yesNo(e.heating), yesNo(e.vehicleName[0]),
               e.vehicleSoc, e.limitSoc, yesNo(e.socBased));
    }
    s += buf;
  }
  const EvccState& h = states[0];
  snprintf(buf, sizeof(buf),
           "Haus: PV %s, Hausakku %s, Netz %s, Prognose %s, Battery Boost in evcc %s, PV-Anlagen %u, Fahrzeuge %u\n",
           yesNo(h.pvPower >= 0), yesNo(h.batterySoc >= 0), yesNo(h.hasGrid), yesNo(h.solarToday >= 0),
           yesNo(h.hasBoost), solar.plantTotal, vehicles.count);
  s += buf;
  if (h.solarToday >= 0) {
    char scale[24] = "aus";
    if (h.solarScale != 1.0f) snprintf(scale, sizeof(scale), "Faktor %.2f", h.solarScale);
    snprintf(buf, sizeof(buf), "Prognose: Rest heute %.1f kWh, morgen %.1f kWh; Anpassung an die echte Erzeugung in evcc: %s\n",
             h.solarToday / 1000.0f, h.solarTomorrow / 1000.0f, scale);
    s += buf;
  }
  if (st.solarPage) {
    snprintf(buf, sizeof(buf), "Solarseite: Stundenwerte %s, heute erzeugt %.1f kWh, Prognose für den ganzen Tag %.1f kWh\n",
             solar.valid ? "vorhanden" : "fehlen", solar.today, solar.forecastToday);
    s += buf;
  }
  return s;
}

// /diag zeigt die Diagnose an, /diag?download=1 liefert sie als Datei zum Anhängen an ein Issue
static void handleDiag() {
  String s;
  if (web.hasArg("download")) {
    s = "Diagnose evcc-Garagendisplay\n"
        "Nicht enthalten: Passwörter, WLAN-Name, Netzwerkadressen, Standort, Namen von Ladepunkten und Fahrzeugen.\n"
        "Bei einem Problem an ein Issue anhängen: " REPO_URL "/issues\n\n";
    web.sendHeader("Content-Disposition", "attachment; filename=\"evcc-garage-diagnose-" FW_VERSION ".txt\"");
  }
  s += diagDevice();
  s += diagDetails();
  web.send(200, "text/plain; charset=utf-8", s);
}

// Einstellungen im Heimnetz ändern, ohne das Portal am Display zu öffnen (WLAN-Daten bleiben unberührt)
static String htmlEscape(const String& in) {
  String out;
  for (char c : in) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

static String option(int value, int selected, const char* label) {
  return String("<option value=\"") + value + "\"" + (value == selected ? " selected" : "") + ">" + label +
         "</option>";
}

static const char SETTINGS_HEAD[] PROGMEM = R"(<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>evcc-Garage</title><style>
:root{color-scheme:light dark;--a:#0fdd8d}
body{font:16px/1.4 system-ui,sans-serif;max-width:30rem;margin:0 auto;padding:1.5rem 1rem}
h1{font-size:1.4rem;margin:0 0 1.5rem}label{display:block;font-weight:600;margin:1.1rem 0 .3rem}
small{display:block;font-weight:400;opacity:.65}input,select{box-sizing:border-box;width:100%;font:inherit;
padding:.6rem;border:1px solid #8888;border-radius:.5rem;background:Field;color:FieldText}
.row{display:flex;gap:.75rem}.row>div{flex:1;min-width:0}button{margin-top:1.8rem;width:100%;font:inherit;font-weight:600;
padding:.8rem;border:0;border-radius:.5rem;background:var(--a);color:#000}footer{margin-top:2rem;opacity:.65;font-size:.85rem}
footer a{color:inherit}h2{font-size:1.05rem;margin:2.2rem 0 0;padding-top:1.1rem;border-top:1px solid #8884}
input[type=checkbox]{width:auto;margin-right:.5rem}.check{font-weight:400}.note{padding:.6rem .8rem;border-radius:.5rem;
background:#0fdd8d33}figure{margin:1.1rem 0 0;text-align:center}figure img{width:15rem;max-width:100%;height:auto;
border-radius:.9rem;border:1px solid #8884;display:block;margin:0 auto .6rem}
button:disabled{opacity:.5}button.plain{margin:0;width:auto;padding:.5rem .9rem;background:none;border:1px solid #8888;color:inherit;font-weight:400}
a.plain{display:inline-block;margin:0 .5rem .5rem 0;padding:.5rem .9rem;border:1px solid #8888;border-radius:.5rem;color:inherit;
text-decoration:none}.hint{font-size:.85rem;opacity:.65;margin:.6rem 0 .9rem}.hint a{color:inherit}
</style></head><body><h1>evcc-Garage</h1>)";

static void handleSettings() {
  if (web.method() == HTTP_POST) {
    // Nur vollständige Formulare übernehmen, sonst stünden leere Felder als 0 in den Einstellungen
    for (const char* key : {"host", "port", "lp", "lat", "lon", "rot", "theme", "timeout", "autoupd"}) {
      if (web.arg(key).isEmpty()) {
        web.send(400, "text/plain; charset=utf-8", String("Feld fehlt: ") + key + "\n");
        return;
      }
    }
    updater::confirm();  // wer hier speichert, erreicht das Display – die Firmware läuft also
    if (!applySettings([](const char* key) { return web.arg(key); })) {
      // Wirkt sofort: neu zeichnen und zurück zur Seite, die dann die neue Anzeige lädt
      lastActivity = millis();
      dirty = true;
      web.sendHeader("Location", "/settings?saved=1");
      web.send(303);
      return;
    }
    restartAt = millis() + 2000;  // Antwortseite noch ausliefern, dann neu starten
    web.sendHeader("Refresh", "15; url=/settings");
    web.send(200, "text/html; charset=utf-8",
             String(FPSTR(SETTINGS_HEAD)) +
                 "<p>Gespeichert. Das Display startet neu, die Seite lädt in einigen Sekunden wieder.</p></body></html>");
    return;
  }

  const Settings& s = settings::get();
  String h = FPSTR(SETTINGS_HEAD);
  h.reserve(7936);
  if (web.hasArg("saved")) h += "<p class=\"note\">Gespeichert – die Änderung ist schon am Display zu sehen.</p>";
  h += "<form method=\"post\"><h2>Verbindung</h2>";
  h += "<div class=\"row\"><div><label>evcc-Adresse</label><input name=\"host\" required value=\"" +
       htmlEscape(s.evccHost) + "\"></div>";
  h += "<div style=\"flex:.45\"><label>Port</label><input name=\"port\" type=\"number\" min=\"1\" max=\"65535\" value=\"" +
       String(s.evccPort) + "\"></div></div>";
  h += "<label>Ladepunkte<small>Nummer wie in evcc (1 = erster). Mehrere mit Komma, z. B. 1,2,3 – dann wischen "
       "zum Wechseln.</small></label><input name=\"lp\" inputmode=\"numeric\" pattern=\"[0-9 ,]*\" value=\"" +
       settings::loadpointsText(s) + "\">";
  h += "<h2>Anzeige</h2><figure><img id=\"shot\" alt=\"Aktuelle Anzeige des Displays\" width=\"480\" height=\"480\" hidden>"
       "<button type=\"button\" class=\"plain\" id=\"load\">Aktuelle Anzeige laden</button></figure>";
  h += "<label>Fahrzeug wechseln<small>Die Fahrzeugzeile über der Leistung antippen, um durch die in evcc angelegten "
       "Fahrzeuge zu schalten.</small></label><select name=\"vehsw\">" + option(1, s.vehicleSwitch, "An") +
       option(0, s.vehicleSwitch, "Aus") + "</select>";
  h += "<label>Ladelimit<small>Zeigt neben dem Fahrzeug das Ladelimit; antippen stellt es für diese Ladung ein. "
       "In Prozent, wenn evcc den Ladestand kennt, sonst als Energie in kWh.</small></label><select name=\"limit\">" +
       option(0, s.chargeLimit, "Aus") + option(1, s.chargeLimit, "An") + "</select>";
  h += "<label>Battery Boost<small>Im Modus Smart die Akku-Anzeige oben antippen, um wie in evcc den Hausakku "
       "zum Laden dazuzuschalten.</small></label><select name=\"boost\">" + option(0, s.boostButton, "Aus") +
       option(1, s.boostButton, "An") + "</select>";
  h += "<label>Solarseite<small>Zusätzliche Seite hinter den Ladepunkten, erreichbar durch Wischen: aktuelle "
       "Erzeugung, der Tag je Stunde (bei mehreren Anlagen die Leistung je Anlage), dazu heute, Rest und morgen. "
       "Tagesverlauf und Tagesertrag liefert evcc ab Version 0.319.</small></label><select name=\"solar\">" +
       option(0, s.solarPage, "Aus") + option(1, s.solarPage, "An") + "</select>";
  h += "<label>Solarseite: große Zahl<small>„Überschuss“ zeigt statt der Erzeugung, was gerade übrig ist – so "
       "gerechnet wie auf der Ladepunktseite ohne Fahrzeug – und darunter den Energiebalken statt der einzelnen "
       "Anlagen. Gedacht für ein Display im Haus: Lohnt es sich gerade, die Spülmaschine einzuschalten?</small></label>"
       "<select name=\"solarsur\">" +
       option(0, s.solarSurplus, "Erzeugung") + option(1, s.solarSurplus, "Überschuss") + "</select>";
  h += "<label>Solarseite zuerst<small>Die Solarseite ist die erste Seite, die Ladepunkte folgen beim Wischen. "
       "Das Display bleibt dann auf ihr stehen, auch wenn ein Fahrzeug angesteckt wird oder zu laden beginnt, "
       "und kehrt zwei Minuten nach der letzten Berührung zu ihr zurück.</small></label><select name=\"solar1st\">" +
       option(0, s.solarFirst, "Aus") + option(1, s.solarFirst, "An") + "</select>";
  h += "<label>Design</label><select name=\"theme\">" + option(0, s.theme, "Automatisch nach Sonnenstand") +
       option(1, s.theme, "Immer hell") + option(2, s.theme, "Immer dunkel") + "</select>";
  char lat[12], lon[12];
  snprintf(lat, sizeof(lat), "%.4f", s.latitude);
  snprintf(lon, sizeof(lon), "%.4f", s.longitude);
  h += "<label>Standort<small>Breiten- und Längengrad, für die automatische Tag/Nacht-Umschaltung.</small></label><div class=\"row\">"
       "<div><input name=\"lat\" type=\"number\" step=\"any\" min=\"-90\" max=\"90\" aria-label=\"Breitengrad\" "
       "placeholder=\"Breitengrad\" value=\"" + String(lat) + "\"></div>"
       "<div><input name=\"lon\" type=\"number\" step=\"any\" min=\"-180\" max=\"180\" aria-label=\"Längengrad\" "
       "placeholder=\"Längengrad\" value=\"" + String(lon) + "\"></div></div>";
  h += "<label>Bildschirm aus nach<small>Minuten ohne Ladung und Bedienung, 0 = immer an.</small></label>"
       "<input name=\"timeout\" type=\"number\" min=\"0\" max=\"240\" value=\"" + String(s.screenTimeoutMin) + "\">";
  h += "<label>Helligkeit</label><select name=\"bright\">";
  for (int pct = 100; pct >= MIN_BRIGHTNESS_PCT; pct -= 10)
    h += option(pct, s.brightnessPct, (String(pct) + " %").c_str());
  h += "</select>";
  h += "<label>Drehung</label><select name=\"rot\">" + option(0, s.rotation, "0°") +
       option(1, s.rotation, "90° im Uhrzeigersinn") + option(2, s.rotation, "180°") +
       option(3, s.rotation, "270° im Uhrzeigersinn") + "</select>";
  h += "<h2>System</h2><label>Automatische Updates</label><select name=\"autoupd\">" + option(1, s.autoUpdate, "An") +
       option(0, s.autoUpdate, "Aus") + "</select>";
  h += String("<label>Passwort<small>Schützt diese Seite und Firmware-Uploads im Heimnetz, Benutzername „admin“. ") +
       (s.password.isEmpty() ? "Aktuell ist keines gesetzt." : "Aktuell ist eines gesetzt; leer lassen, um es zu behalten.") +
       "</small></label><input name=\"pw\" type=\"password\" autocomplete=\"new-password\" maxlength=\"" +
       String(MAX_PASSWORD_LEN) + "\" placeholder=\"" + (s.password.isEmpty() ? "Kein Passwort" : "Unverändert") + "\">";
  if (!s.password.isEmpty())
    h += "<label class=\"check\"><input type=\"checkbox\" name=\"pwclear\" value=\"1\">Passwort entfernen</label>";
  h += "<button>Speichern</button><small>Verbindung, Drehung und Passwort starten das Display neu, alles andere "
       "wirkt sofort.</small></form>";
  h += "<h2>Firmware</h2><p class=\"hint\">Installiert ist Version <span id=\"fwv\">" FW_VERSION "</span>. Neue Versionen holt das Display "
       "von selbst, wenn automatische Updates an sind – sobald es zehn Minuten nicht gebraucht wird und kein Auto "
       "lädt. Hier geht es sofort; das Display startet dabei neu, eine laufende Ladung läuft weiter.</p>"
       "<button type=\"button\" class=\"plain\" id=\"upd\">Jetzt aktualisieren</button>"
       "<p class=\"hint\" id=\"updmsg\" hidden></p>";
  h += "<h2>Diagnose</h2><p class=\"hint\">Hilft bei der Fehlersuche: Firmware, Hardware, Einstellungen, die letzten "
       "Starts und Abstürze. Die Datei enthält keine Passwörter, keinen WLAN-Namen, keine Netzwerkadressen, keinen "
       "Standort und keine Namen von Ladepunkten oder Fahrzeugen. Du kannst sie so an ein "
       "<a href=\"" REPO_URL "/issues\">Issue auf GitHub</a> anhängen.</p>"
       "<a class=\"plain\" href=\"/diag?download=1\">Diagnose herunterladen</a>"
       "<a class=\"plain\" href=\"/diag\">Ansehen</a>";
  h += "<script>const i=document.getElementById('shot'),b=document.getElementById('load');"
       "function l(){b.textContent='Lädt …';i.onload=()=>{i.hidden=false;b.textContent='Aktualisieren'};"
       "i.onerror=()=>{b.textContent='Erneut versuchen'};i.src='/screenshot.bmp?'+Date.now()}"
       "b.onclick=l;if(location.search.includes('saved'))setTimeout(l,800)</script>";
  // "Jetzt aktualisieren": Prüfung und Installation anstoßen, dann /update abfragen, bis das Ergebnis feststeht.
  // Während des Neustarts antwortet das Display nicht; läuft danach eine andere Version, ist es geschafft.
  h += "<script>{const u=document.getElementById('upd'),m=document.getElementById('updmsg'),v0='" FW_VERSION "';"
       "const say=t=>{m.hidden=false;m.textContent=t};"
       "u.onclick=async()=>{u.disabled=true;let n=0;say('Suche nach einer neuen Version\u00a0…');"
       "try{await fetch('/update?check=1&install=1')}catch(e){}"
       "const t=setInterval(async()=>{const end=x=>{clearInterval(t);u.disabled=false;if(x)say(x)};"
       "if(++n>120)return end('Keine Antwort vom Display. Bitte Seite neu laden.');"
       "let x;try{x=await(await fetch('/update')).text()}catch(e){return say('Das Display startet neu\u00a0…')}"
       "const v=(x.match(/^Firmware (.*)$/m)||[])[1],r=(x.match(/^Letzte Prüfung: (.*)$/m)||[])[1]||'';"
       "if(v&&v!=v0){document.getElementById('fwv').textContent=v;return end('Aktualisiert auf Version '+v+'.')}"
       "if(r=='läuft …'||r=='noch nicht geprüft')say('Suche nach einer neuen Version\u00a0…');"
       "else if(r.startsWith('Installation')||r.startsWith('neue Version')||r.includes('installiert'))"
       "say('Neue Version wird installiert, danach startet das Display neu\u00a0…');"
       "else end(r=='aktuell'?'Die Firmware ist aktuell.':r)},2000)}}</script>";
  h += "<footer>Firmware " FW_VERSION
       " · WLAN ändern: Titel am Display 3 s gedrückt halten<br><a href=\"" PROJECT_URL "\">Projektseite und Anleitung</a>"
       " · <a href=\"" REPO_URL "\">GitHub</a></footer></body></html>";
  web.send(200, "text/html; charset=utf-8", h);
}

// Ist ein Passwort gesetzt, verlangen alle Seiten die Anmeldung als "admin"
static void guarded(const char* path, void (*handler)()) {
  web.on(path, [handler]() {
    const String& password = settings::get().password;
    if (!password.isEmpty() && !web.authenticate(WEB_USER, password.c_str())) {
      web.requestAuthentication(BASIC_AUTH, "evcc-Garage");
      return;
    }
    handler();
  });
}

static void startWebServer() {
  web.on("/", []() {
    web.sendHeader("Location", "/settings");
    web.send(302);
  });
  guarded("/settings", handleSettings);
  guarded("/screenshot.bmp", []() { display::sendScreenshot(web); });
  guarded("/preview", handlePreview);
  // Update-Status; ?check=1 prüft sofort, ?install=1 installiert eine gefundene neue Version sofort
  guarded("/update", []() {
    if (web.hasArg("check")) updater::checkNow();
    if (web.hasArg("install")) updater::installNow();
    web.send(200, "text/plain; charset=utf-8", updater::status());
  });
  // Laufzeit, Neustartgrund, Speicher und der zuletzt gespeicherte Absturz (Coredump im Flash)
  guarded("/diag", handleDiag);
  // Bildschirm von außen schalten, z. B. per Bewegungsmelder aus Home Assistant oder ioBroker.
  // /wake wirkt wie eine Berührung, /sleep schaltet aus bis zum nächsten Aufwecken.
  guarded("/wake", []() {
    lastActivity = millis();
    web.send(200, "text/plain; charset=utf-8", "Bildschirm an\n");
  });
  guarded("/sleep", []() {
    forcedSleep = true;
    forcedSleepActivity = lastActivity;
    web.send(200, "text/plain; charset=utf-8", "Bildschirm aus\n");
  });
  // Taktung der Hintergrundbeleuchtung ausprobieren (#21): manche Boards bleiben mit der schnellen Taktung
  // unter 90 % Helligkeit dunkel. ?hz=… (100 bis 40000) gilt bis zum Neustart und schaltet den Bildschirm an.
  // Mit &save=1 wird der Wert gespeichert und das Display startet neu: Dann gilt die Taktung schon ab dem
  // Start, ohne Umschalten im laufenden Betrieb.
  guarded("/backlight", []() {
    String s;
    if (web.hasArg("hz")) {
      const uint32_t hz = constrain(web.arg("hz").toInt(), (long)MIN_BACKLIGHT_HZ, (long)MAX_BACKLIGHT_HZ);
      if (web.arg("save") == "1") {
        settings::get().backlightHz = hz;
        settings::save();
        restartAt = millis() + 2000;  // Antwort noch ausliefern, dann neu starten
        web.send(200, "text/plain; charset=utf-8",
                 "Gespeichert: Taktung " + String(hz) + " Hz. Das Display startet jetzt neu und nutzt sie ab dem Start.\n");
        return;
      }
      if (!display::setBacklightHz(hz)) s = String(hz) + " Hz lässt sich nicht einstellen.\n";
      lastActivity = millis();
    }
    s += "Hintergrundbeleuchtung: Taktung " + String(display::backlightHz()) + " Hz (gespeichert für den Start: " +
         String(settings::get().backlightHz) + " Hz), Helligkeit " + String(settings::get().brightnessPct) + " %\n";
    web.send(200, "text/plain; charset=utf-8", s);
  });
  // Lasttest gegen Bildstörungen: löst eine WLAN-Suche aus (wie das Einstellungsportal)
  guarded("/wifiscan", []() {
    const uint32_t start = millis();
    const int n = WiFi.scanNetworks();
    web.send(200, "text/plain", String(n) + " Netze in " + String(millis() - start) + " ms\n");
  });
  web.begin();
}

static void startNetworkServices() {
  if (!timeStarted) {
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
    timeStarted = true;
  }
  if (!webStarted) {
    startWebServer();
    webStarted = true;
  }
  if (!otaStarted) {
    ArduinoOTA.setHostname(HOSTNAME);
    // Das Passwort aus den Einstellungen gilt auch für Firmware-Uploads im Heimnetz
    if (!settings::get().password.isEmpty()) ArduinoOTA.setPassword(settings::get().password.c_str());
#ifdef OTA_PASSWORD
    else
      ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
    // Den Neustart nach dem Upload übernimmt der loop, damit er wie alle anderen abläuft
    ArduinoOTA.setRebootOnSuccess(false);
    ArduinoOTA.onEnd([]() { restartAt = millis() + 500; });
    ArduinoOTA.onStart([]() {
      display::setBrightness(settings::backlight());
      ui::showMessage(ICON_SETTINGS, "Update", "Neue Firmware wird installiert …");
      lv_refr_now(nullptr);
    });
    // Bricht der Upload ab, bliebe der Hinweis sonst bis zum Neustart stehen
    ArduinoOTA.onError([](ota_error_t) {
      if (portalActive)
        showPortalMessage();
      else
        ui::showMain();
    });
    ArduinoOTA.begin();
    otaStarted = true;
  }
}

// ---------- Arduino ----------

void setup() {
  Serial.begin(115200);

  recordBoot();
  settings::load();
  solarShown = settings::get().solarPage && settings::get().solarFirst;
  display::begin(settings::get().rotation, settings::get().backlightHz);
  brightness = settings::backlight();
  lastActivity = millis();

  ui::begin(onModeSelected, openSettings, onSwipe, onBoostTapped, onVehicleTapped, onLimitSelected);
  ui::onMessageTap(closeSettings);
  ui::showMessage(ICON_SETTINGS, "Verbinde …", "WLAN-Verbindung wird aufgebaut");
  lv_refr_now(nullptr);
  display::setBrightness(settings::backlight());  // erst einschalten, wenn ein Bild im Speicher steht

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // kein Modem-Sleep am Netzteil: verhindert verpasste Schlüsselwechsel im WLAN
  WiFi.setAutoReconnect(true);
  setupPortal();
  if (wm.autoConnect(PORTAL_AP_NAME)) {
    ui::showMain();
  } else {
    portalActive = true;
    ui::showMessage(ICON_WIFI_OFF, "WLAN einrichten",
                    "Mit dem WLAN \"" PORTAL_AP_NAME "\" verbinden. Die Seite öffnet sich automatisch, "
                    "sonst 192.168.4.1 aufrufen.");
  }
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        wifiDisconnects = wifiDisconnects + 1;
        lastDisconnectReason = info.wifi_sta_disconnected.reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  evcc::begin();
  updater::begin();
}

// Stößt eine WLAN-Neuverbindung an, wenn AutoReconnect nach einer Trennung (z. B. Mesh-Steering,
// abgelaufener DHCP-Lease oder kurzzeitig nicht bereiter Router) nicht gegriffen hat oder trotz
// bestehender Funkverbindung dauerhaft keine Daten mehr von evcc kommen.
static void maintainWifi() {
  static uint32_t lastAttempt = 0;
  static bool wasConnected = false;
  const uint32_t now = millis();
  if (portalActive) {
    lastAttempt = now;
    return;
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wasConnected) {
    wasConnected = connected;
    lastAttempt = now;
  }
  if (!connected) {
    if (now - lastAttempt >= WIFI_RECONNECT_MS) {
      lastAttempt = now;
      WiFi.disconnect();
      WiFi.reconnect();
    }
    return;
  }
  // Ist evcc länger aus, hilft das Neuverbinden nicht: nach dem ersten Versuch nur noch selten,
  // und nie während eines Firmware-Downloads
  static bool retried = false;
  if (states[0].updatedMs != 0 && now - states[0].updatedMs < STALE_AFTER_MS) {
    lastAttempt = now;
    retried = false;
  } else if (updater::working()) {
    lastAttempt = now;
  } else if (now - lastAttempt >= (retried ? WIFI_STALE_RETRY_MS : WIFI_STALE_RECONNECT_MS)) {
    lastAttempt = now;
    retried = true;
    WiFi.disconnect();
    WiFi.reconnect();
  }
}

// Automatische Updates nur, wenn das Display gerade niemand braucht
static void handleUpdates() {
  const bool idle = !portalActive && !previewActive() && !anyPending() && !anyCharging() &&
                    millis() - lastActivity >= UPDATE_IDLE_MS;
  switch (updater::loop(idle)) {
    case updater::Event::Installing: {
      static char text[80];
      snprintf(text, sizeof(text), "Version %s wird geladen und installiert …", updater::availableVersion());
      ui::showMessage(ICON_SETTINGS, "Update", text);
      break;
    }
    case updater::Event::Done:
      restartAt = millis() + 1000;
      break;
    case updater::Event::Failed:
      ui::showMain();
      break;
    default:
      break;
  }
}

void loop() {
  lv_timer_handler();
  wm.process();
  maintainWifi();

  if (WiFi.status() == WL_CONNECTED) {
    startNetworkServices();
    ArduinoOTA.handle();
    web.handleClient();
  }
  // Steht ein Neustart an (Update installiert, Firmware hochgeladen, Einstellungen gespeichert), beginnt
  // kein Update mehr: Es schriebe in den Platz, aus dem das Display gleich starten soll
  if (!restartAt) handleUpdates();

  if (restartAt && (int32_t)(millis() - restartAt) > 0) restartNow();

  if (portalActive && !wm.getConfigPortalActive()) {
    portalActive = false;
    // Portal ohne WLAN abgelaufen (z. B. Router nach Stromausfall noch nicht bereit) → neu versuchen
    if (WiFi.status() != WL_CONNECTED) restartNow();
    ui::showMain();
  }

  static uint32_t lastTick = 0;
  if (millis() - lastTick >= 20) {
    lastTick = millis();
    updateBacklight();
    static uint8_t n = 0;
    if (++n >= 10) {  // alle 200 ms
      n = 0;
      updateState();
    }
  }

  delay(2);
}
