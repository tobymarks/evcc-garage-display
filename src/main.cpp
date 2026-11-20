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
#include <stdarg.h>
#include <functional>
#include <lvgl.h>

#include "board.h"
#include "config.h"
#include "display.h"
#include "evcc_client.h"
#include "fw_version.h"
#include "icons.h"
#include "lang.h"
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
static WiFiManagerParameter* pLanguage;
static WiFiManagerParameter* pName;
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
// Bausteine der Solarseite in der Vorschau (?cfg=groß,mitte,k1,k2,k3 oder ?surplus=0|1), -1 = Einstellung
static int8_t previewSolarCfg[5] = {-1, -1, -1, -1, -1};

// ---------- WLAN-Einrichtung / Einstellungen ----------

// Übernimmt die Werte aus dem Portal oder von /settings (gleiche Feldnamen).
// Liefert true, wenn die Änderung erst nach einem Neustart wirkt (Verbindung, Drehung, Passwort, Taktung, Sprache,
// Name im Heimnetz);
// alles andere liest die Oberfläche laufend aus den Einstellungen.
static bool applySettings(std::function<String(const char*)> value) {
  Settings& s = settings::get();
  const String oldHost = s.evccHost, oldLps = settings::loadpointsText(s), oldPassword = s.password,
               oldHostname = s.hostname;
  const uint16_t oldPort = s.evccPort;
  const uint8_t oldRotation = s.rotation;
  const uint32_t oldBacklightHz = s.backlightHz;
  const uint8_t oldLanguage = s.language;
  s.evccHost = value("host");
  s.evccHost.trim();
  // Nur der Rechnername zählt: "http://" davor und einen Pfad dahinter verzeihen
  const int scheme = s.evccHost.indexOf("://");
  if (scheme >= 0) s.evccHost.remove(0, scheme + 3);
  const int slash = s.evccHost.indexOf('/');
  if (slash >= 0) s.evccHost.remove(slash);
  s.evccPort = constrain(value("port").toInt(), 1, 65535);
  settings::parseLoadpoints(value("lp").c_str(), s);
  // Leer gelassen bleibt der Standort, wie er ist
  if (!value("lat").isEmpty()) s.latitude = value("lat").toFloat();
  if (!value("lon").isEmpty()) s.longitude = value("lon").toFloat();
  s.rotation = constrain(value("rot").toInt(), 0, 3);
  s.theme = constrain(value("theme").toInt(), 0, 2);
  s.screenTimeoutMin = constrain(value("timeout").toInt(), 0, 240);
  s.autoUpdate = value("autoupd").toInt() != 0;
  // Fehlt im WLAN-Portal – dort bleibt die bisherige Einstellung
  const String boost = value("boost");
  if (!boost.isEmpty()) s.boostButton = boost.toInt() != 0;
  const String bright = value("bright");
  if (!bright.isEmpty()) s.brightnessPct = constrain(bright.toInt(), MIN_BRIGHTNESS_PCT, 100);
  // Ein eigener Wert aus /backlight?hz=…&save=1 bleibt stehen, solange der Schalter an ist
  const String slowBacklight = value("slowbl");
  if (!slowBacklight.isEmpty()) {
    if (slowBacklight.toInt() == 0)
      s.backlightHz = LCD_BACKLIGHT_HZ;
    else if (s.backlightHz == LCD_BACKLIGHT_HZ)
      s.backlightHz = SLOW_BACKLIGHT_HZ;
  }
  const String chargeLimit = value("limit");
  if (!chargeLimit.isEmpty()) s.chargeLimit = chargeLimit.toInt() != 0;
  const String vehicleSwitch = value("vehsw");
  if (!vehicleSwitch.isEmpty()) s.vehicleSwitch = vehicleSwitch.toInt() != 0;
  const String solarPage = value("solar");
  if (!solarPage.isEmpty()) s.solarPage = solarPage.toInt() != 0;
  const String solarFirst = value("solar1st");
  if (!solarFirst.isEmpty()) s.solarFirst = solarFirst.toInt() != 0;
  // Sprache: im Formular die Nummer, im WLAN-Portal das Kürzel ("de", "en")
  String language = value("lang");
  language.trim();
  language.toLowerCase();
  if (language == "en" || language == "1")
    s.language = (uint8_t)Lang::En;
  else if (language == "de" || language == "0")
    s.language = (uint8_t)Lang::De;
  // Name im Heimnetz; leer oder ohne brauchbares Zeichen bleibt der bisherige
  const String hostname = settings::cleanHostname(value("name"));
  if (!hostname.isEmpty()) s.hostname = hostname;
  // Leeres Feld = Passwort unverändert; entfernt wird es nur ausdrücklich
  const String password = value("pw");
  if (value("pwclear").toInt() != 0)
    s.password = "";
  else if (!password.isEmpty())
    s.password = password.substring(0, MAX_PASSWORD_LEN);
  settings::save();
  return s.evccHost != oldHost || s.evccPort != oldPort || settings::loadpointsText(s) != oldLps ||
         s.rotation != oldRotation || s.password != oldPassword || s.backlightHz != oldBacklightHz ||
         s.language != oldLanguage || s.hostname != oldHostname;
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

  // Die Sprache steht oben und in beiden Sprachen da: Ein neues Display startet auf Englisch
  pLanguage = new WiFiManagerParameter("lang", "Sprache / Language: de = Deutsch, en = English", lang::code(), 2);
  pHost = new WiFiManagerParameter("host", tr("evcc-Adresse"), s.evccHost.c_str(), 40);
  pPort = new WiFiManagerParameter("port", tr("evcc-Port"), port, 5);
  pLoadpoint = new WiFiManagerParameter(
      "lp", tr("Ladepunkt-Nr. (1 = erster in evcc). Mehrere mit Komma, z. B. 1,2,3 – dann wischen zum Wechseln"), lp, 17);
  pLat = new WiFiManagerParameter("lat", tr("Breitengrad (für Tag/Nacht)"), lat, 11);
  pLon = new WiFiManagerParameter("lon", tr("Längengrad (für Tag/Nacht)"), lon, 11);
  pRotate = new WiFiManagerParameter("rot", tr("Drehung: 0–3 (je 90° im Uhrzeigersinn)"), rot, 1);
  pTheme = new WiFiManagerParameter("theme", tr("Design: 0 = automatisch (Sonnenstand), 1 = immer hell, 2 = immer dunkel"), theme, 1);
  pTimeout = new WiFiManagerParameter("timeout", tr("Bildschirm aus nach Minuten ohne Ladung und Bedienung (0 = immer an)"), timeout, 3);
  pAutoUpdate = new WiFiManagerParameter("autoupd", tr("Automatische Updates: 1 = an, 0 = aus"), autoUpdate, 1);
  pName = new WiFiManagerParameter(
      "name", tr("Name im Heimnetz (a–z, 0–9, -). Bei mehreren Displays je einen eigenen, z. B. evcc-kueche"),
      s.hostname.c_str(), MAX_HOSTNAME_LEN);
  static WiFiManagerParameter version("<p>Firmware " FW_VERSION "</p>");
  for (auto* p : {pLanguage, pHost, pPort, pLoadpoint, pLat, pLon, pRotate, pTheme, pTimeout, pAutoUpdate, pName, &version})
    wm.addParameter(p);

  wm.setTitle("evcc-Garage");
  wm.setHostname(s.hostname);
  static std::vector<const char*> menu = {"wifi", "param", "info", "restart"};
  wm.setMenu(menu);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(20);
  wm.setSaveParamsCallback(saveParams);
}

static void showPortalMessage() {
  char text[200];
  snprintf(text, sizeof(text),
           tr("Mit dem WLAN \"%s\" verbinden. Die Seite öffnet sich automatisch, sonst 192.168.4.1 aufrufen. "
              "Antippen zum Abbrechen."),
           PORTAL_AP_NAME);
  ui::showMessage(ICON_SETTINGS, tr("Einstellungen"), text);
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
  const char* targetTitle = next < vehicles.count ? vehicles.titles[next] : tr("Gastfahrzeug");
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
  static bool lastConnected[MAX_LOADPOINTS], lastCharging[MAX_LOADPOINTS], stateKnown[MAX_LOADPOINTS];
  static bool lastWifi = false, lastStale = true;
  static char lastClock[6] = "";

  if (evcc::latest(states, vehicles, solar)) dirty = true;
  if (solarShown && !settings::get().solarPage) {  // in den Einstellungen ausgeschaltet
    solarShown = false;
    dirty = true;
  }
  // Eine gewählte Ladepunktseite bleibt stehen, bis der Bildschirm ausgeht (#53). Bleibt er trotz Timeout wegen einer Ladung an,
  // geht es nach der eingestellten Zeit zurück zur Startseite, frühestens nach SOLAR_RETURN_MS.
  if (solarFirst() && !solarShown) {
    const uint32_t timeoutMs = settings::get().screenTimeoutMin * 60000UL;
    if (brightness == 0 || (timeoutMs && millis() - lastActivity > max(timeoutMs, SOLAR_RETURN_MS))) {
      solarShown = true;
      flashMsg = nullptr;
      dirty = true;
    }
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
    if ((e = checkPending(Command::Mode, i, st.mode == modeTarget[i], TR_KEY("Modus konnte nicht gesetzt werden"),
                          TR_KEY("evcc hat den Modus nicht übernommen"))))
      error = e;
    if ((e = checkPending(Command::Boost, i, st.batteryBoost == boostTarget[i],
                          TR_KEY("Boost konnte nicht geschaltet werden"), TR_KEY("evcc hat den Boost nicht übernommen"))))
      error = e;
    if ((e = checkPending(Command::Vehicle, i, !strcmp(st.vehicleName, vehicleTargetName[i]),
                          TR_KEY("Fahrzeug konnte nicht gewechselt werden"), TR_KEY("evcc hat das Fahrzeug nicht übernommen"))))
      error = e;
    Pending& lim = pending(Command::Limit, i);
    if (lim.active && st.valid && !st.connected) {
      lim.active = false;  // abgesteckt: evcc setzt das Limit ohnehin zurück
      dirty = true;
    }
    const bool limitReached = limitTargetEnergy[i] ? fabsf(st.limitEnergy - limitTarget[i]) < 0.05f
                                                   : lroundf(st.limitSoc) == lroundf(limitTarget[i]);
    if ((e = checkPending(Command::Limit, i, limitReached, TR_KEY("Ladelimit konnte nicht gesetzt werden"),
                          TR_KEY("evcc hat das Ladelimit nicht übernommen"))))
      error = e;
    if (error) {
      if (i == current) flash(error);
      dirty = true;
    }

    // Die ersten Daten nach dem Start sind keine Änderung: Das Display beginnt auf der ersten Seite (#54)
    if (st.valid && !stateKnown[i]) {
      stateKnown[i] = true;
      lastConnected[i] = st.connected;
      lastCharging[i] = st.charging;
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
    {
      const Settings& st = settings::get();
      const uint8_t saved[5] = {st.solarBig, st.solarMid, st.solarTile[0], st.solarTile[1], st.solarTile[2]};
      uint8_t cfg[5];
      for (int i = 0; i < 5; i++) cfg[i] = preview && previewSolarCfg[i] >= 0 ? previewSolarCfg[i] : saved[i];
      v.solarBig = cfg[0];
      v.solarMid = cfg[1];
      memcpy(v.solarTile, cfg + 2, 3);
    }
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
  for (int8_t& c : previewSolarCfg) c = -1;
  if (web.hasArg("cfg")) {
    // wie die Felder von /solar: große Zahl, Mitte, drei Kacheln
    static const uint8_t LIMIT[5] = {SOLAR_BIG_COUNT, SOLAR_MID_COUNT, SOLAR_TILE_COUNT, SOLAR_TILE_COUNT, SOLAR_TILE_COUNT};
    const String cfg = web.arg("cfg");
    int pos = 0;
    for (int i = 0; i < 5 && pos <= (int)cfg.length(); i++) {
      const int comma = cfg.indexOf(',', pos);
      const int value = cfg.substring(pos, comma < 0 ? cfg.length() : comma).toInt();
      previewSolarCfg[i] = constrain(value, 0, LIMIT[i] - 1);
      if (comma < 0) break;
      pos = comma + 1;
    }
  } else if (web.hasArg("surplus")) {
    const bool on = web.arg("surplus").toInt() != 0;
    previewSolarCfg[0] = on ? SOLAR_BIG_SURPLUS : SOLAR_BIG_PV;
    previewSolarCfg[1] = on ? SOLAR_MID_FLOW : SOLAR_MID_PLANTS;
  }
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
  snprintf(buf, sizeof(buf), "Letzte Aktivität vor %lu s, Timeout %u min, /sleep %s, Ladebetrieb %s\n",
           (unsigned long)((millis() - lastActivity) / 1000), settings::get().screenTimeoutMin,
           forcedSleep ? "aktiv" : "inaktiv", anyCharging() ? "ja" : "nein");
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
           "Battery Boost %s, Fahrzeugwechsel %s, Ladelimit %s, Solarseite %s (Bausteine %u/%u/%u-%u-%u, zuerst %s), "
           "Passwort gesetzt %s, Standort gesetzt %s, Sprache %s, eigener Name im Heimnetz %s\n",
           yesNo(st.boostButton), yesNo(st.vehicleSwitch), yesNo(st.chargeLimit), yesNo(st.solarPage),
           st.solarBig, st.solarMid, st.solarTile[0], st.solarTile[1], st.solarTile[2], yesNo(st.solarFirst),
           yesNo(!st.password.isEmpty()), yesNo(st.latitude != 0 || st.longitude != 0), lang::code(),
           yesNo(st.hostname != HOSTNAME));
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

static const char SETTINGS_HEAD[] PROGMEM = R"(<!doctype html><html lang="{{@lang}}"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>{{Einstellungen}} · evcc-Garage</title><style>
:root{color-scheme:light dark;--a:#0fdd8d}[hidden]{display:none!important}
body{font:16px/1.4 system-ui,sans-serif;max-width:30rem;margin:0 auto;padding:1.5rem 1rem}
h1{font-size:1.4rem;margin:0 0 1.5rem}h1 span{font-weight:400;opacity:.65;margin-left:.5rem}
label{display:block;font-weight:600;margin:1.1rem 0 .3rem}small{display:block;font-weight:400;opacity:.65}
input,select{box-sizing:border-box;width:100%;min-height:2.75rem;font:inherit;padding:.6rem;border:1px solid #8888;
border-radius:.5rem;background:Field;color:FieldText}
.row{display:flex;gap:.75rem}.row>div{flex:1;min-width:0}.row{margin-top:1.1rem}.row label{margin-top:0}label.mini{font-weight:400;font-size:.85rem;margin:0 0 .2rem}
button{width:100%;min-height:2.75rem;font:inherit;font-weight:600;padding:.8rem;border:0;border-radius:.5rem;
background:var(--a);color:#000}button:disabled{opacity:.5}
h2{font-size:1.05rem;margin:2.2rem 0 0;padding-top:1.1rem;border-top:1px solid #8884}
.sw{display:flex;align-items:center;gap:1rem;cursor:pointer}.sw span{flex:1}
.sw input{appearance:none;flex:none;width:3rem;min-height:0;height:1.75rem;padding:0;border:0;border-radius:1rem;
background:#8889;position:relative;cursor:pointer;transition:background .15s}
.sw input::after{content:"";position:absolute;top:.2rem;left:.2rem;width:1.35rem;height:1.35rem;border-radius:50%;
background:#fff;box-shadow:0 1px 2px #0006;transition:transform .15s}
.sw input:checked{background:var(--a)}.sw input:checked::after{transform:translateX(1.25rem)}
.sw input:focus-visible{outline:2px solid;outline-offset:2px}
fieldset{border:0;margin:0;padding:0 0 0 1rem;border-left:2px solid #8884;min-width:0}fieldset:disabled{opacity:.45}
.check{font-weight:400;display:flex;align-items:center;gap:.5rem;margin:.6rem 0 0}
.check input{width:1.15rem;min-height:0;height:1.15rem;margin:0}
.note{padding:.6rem .8rem;border-radius:.5rem;background:#0fdd8d33}
.st{margin:.9rem 0 0;font-size:.9rem}.st::before{content:"";display:inline-block;width:.6rem;height:.6rem;
border-radius:50%;margin-right:.5rem;background:#0bb866}.st.bad::before{background:#e8590c}
figure{margin:1.1rem 0 0;text-align:center}figure img{width:15rem;max-width:100%;height:auto;border-radius:.9rem;
border:1px solid #8884;display:block;margin:0 auto .6rem}
.plain{display:inline-flex;align-items:center;box-sizing:border-box;width:auto;min-height:2.75rem;margin:0 .5rem .5rem 0;
padding:.5rem .9rem;background:none;border:1px solid #8888;border-radius:.5rem;color:inherit;font-weight:400;text-decoration:none}
.hint{font-size:.85rem;opacity:.65;margin:.6rem 0 .9rem}.hint a{color:inherit}
.bar{position:sticky;bottom:0;margin:1.8rem -1rem 0;padding:.75rem 1rem;background:Canvas;border-top:1px solid #8884}
.bar small{margin-bottom:.5rem;opacity:1;font-weight:600}.bar a{color:inherit;font-weight:400}
footer{margin-top:2rem;opacity:.65;font-size:.85rem}footer a{color:inherit}
</style></head><body><h1>evcc-Garage<span>{{Einstellungen}}</span></h1>)";

// Beschriftung eines Felds, mit ihm verbunden (Antippen setzt den Cursor, Vorleseprogramme nennen den Namen)
static String field(const char* id, const char* label, const char* hint = nullptr) {
  String out = String("<label for=\"") + id + "\">" + label;
  if (hint) out += String("<small>") + hint + "</small>";
  return out + "</label>";
}

// Schalter. Ein Kästchen ohne Haken schickt der Browser gar nicht mit, deshalb folgt ein verstecktes Feld
// mit 0: web.arg() liefert den ersten Wert – also 1, wenn der Haken gesetzt ist.
static String toggle(const char* name, bool on, const char* label, const char* hint) {
  return String("<label class=\"sw\"><span>") + label + "<small>" + hint + "</small></span><input type=\"checkbox\" "
         "role=\"switch\" name=\"" + name + "\" value=\"1\"" + (on ? " checked" : "") + "><input type=\"hidden\" name=\"" +
         name + "\" value=\"0\"></label>";
}

// Text mit Platzhaltern als String, für übersetzte Sätze mit Zahlen oder Namen
static String fmt(const char* format, ...) {
  char buf[400];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

// Seitenvorlage ausfüllen: Ein deutscher Text in doppelten geschweiften Klammern wird durch die Übersetzung
// ersetzt, dazu {{@lang}} (Sprachkürzel), {{@locale}} (Zeitformat im Browser), {{@dec}} (Dezimaltrennzeichen)
// und {{@pct}} (Prozentzeichen mit Abstand).
// Die Texte stehen in den Vorlagen in HTML und in JavaScript mit einfachen Anführungszeichen: Übersetzungen
// dürfen deshalb kein ' enthalten (prüft tools/check_lang.py).
static String page(const char* tmpl) {
  String out;
  out.reserve(strlen(tmpl) + 512);
  // Die Klammern einzeln, damit tools/check_lang.py diese Zeilen nicht für einen Vorlagentext hält
  static const char OPEN[] = {'{', '{', 0}, CLOSE[] = {'}', '}', 0};
  const char* p = tmpl;
  for (;;) {
    const char* open = strstr(p, OPEN);
    const char* close = open ? strstr(open, CLOSE) : nullptr;
    if (!close) break;
    out.concat(p, open - p);
    String key;
    key.concat(open + 2, close - open - 2);
    if (key == "@lang")
      out += lang::code();
    else if (key == "@locale")
      out += lang::locale();
    else if (key == "@dec")
      out += lang::decimal();
    else if (key == "@pct")
      out += lang::get() == Lang::De ? " %" : "%";
    else
      out += tr(key.c_str());
    p = close + 2;
  }
  out += p;
  return out;
}

static String timeoutLabel(int minutes) {
  if (!minutes) return tr("Nie");
  if (minutes == 1) return tr("Nach 1 Minute");
  if (minutes == 60) return tr("Nach 1 Stunde");
  if (minutes % 60 == 0) return fmt(tr("Nach %d Stunden"), minutes / 60);
  return fmt(tr("Nach %d Minuten"), minutes);
}

// Bild vom Display laden; abhängige Felder, ungespeicherte Änderungen und der Hinweis auf den Neustart
static const char SETTINGS_SCRIPT[] PROGMEM = R"JS(<script>const i=document.getElementById('shot'),b=document.getElementById('load');
function l(){b.textContent='{{Lädt …}}';i.onload=()=>{i.hidden=false;b.textContent='{{Aktualisieren}}'};
i.onerror=()=>{b.textContent='{{Erneut versuchen}}'};i.src='/screenshot.bmp?'+Date.now()}
b.onclick=l;if(location.search.includes('saved'))setTimeout(l,800)</script>
<script>{const f=document.forms[0],$=s=>document.querySelector(s),bt=$('.bar button'),dn=$('#dirty');
const sync=()=>{$('#solarsub').disabled=!$('input[name=solar]').checked;$('#loc').hidden=$('#theme').value!='0'};
const snap=()=>new URLSearchParams(new FormData(f)).toString();sync();const s0=snap(),f0=new FormData(f);let sub=0;
const ts=$('#tosolar');
const upd=()=>{sync();const fd=new FormData(f);dn.hidden=snap()==s0;
ts.textContent=dn.hidden?'{{Solarseite einrichten}}':'{{Speichern und Solarseite einrichten}}';
bt.textContent=['host','port','lp','rot','pw','pwclear','slowbl','lang','name'].some(k=>(fd.get(k)||'')!=(f0.get(k)||''))
?'{{Speichern und neu starten}}':'{{Speichern}}'};
f.addEventListener('input',upd);f.addEventListener('change',upd);
$('#undo').onclick=e=>{e.preventDefault();f.reset();$('#pw').type='password';upd()};
$('#pwshow').onchange=e=>{$('#pw').type=e.target.checked?'text':'password'};
f.onsubmit=()=>{sub=1};
ts.onclick=e=>{if(dn.hidden)return;e.preventDefault();const n=document.createElement('input');
n.type='hidden';n.name='next';n.value='solar';f.append(n);if(f.reportValidity()){sub=1;f.submit()}else n.remove()};
addEventListener('beforeunload',e=>{if(!dn.hidden&&!sub){e.preventDefault();e.returnValue=''}});
if(location.search.includes('saved'))history.replaceState(null,'','/settings')}</script>)JS";

// "Jetzt aktualisieren": Prüfung und Installation anstoßen, dann /update abfragen, bis das Ergebnis feststeht.
// Während des Neustarts antwortet das Display nicht; läuft danach eine andere Version, ist es geschafft.
// /update antwortet immer deutsch (siehe lang.h), deshalb bleiben die Suchmuster deutsch.
static const char UPDATE_SCRIPT[] PROGMEM = R"JS(<script>{const u=document.getElementById('upd'),m=document.getElementById('updmsg'),v0=')JS" FW_VERSION R"JS(';
const say=t=>{m.hidden=false;m.textContent=t};
u.onclick=async()=>{u.disabled=true;let n=0;say('{{Suche nach einer neuen Version …}}');
try{await fetch('/update?check=1&install=1')}catch(e){}
const t=setInterval(async()=>{const end=x=>{clearInterval(t);u.disabled=false;if(x)say(x)};
if(++n>120)return end('{{Keine Antwort vom Display. Bitte Seite neu laden.}}');
let x;try{x=await(await fetch('/update')).text()}catch(e){return say('{{Das Display startet neu …}}')}
const v=(x.match(/^Firmware (.*)$/m)||[])[1],r=(x.match(/^Letzte Prüfung: (.*)$/m)||[])[1]||'';
if(v&&v!=v0){document.getElementById('fwv').textContent=v;return end('{{Aktualisiert auf Version %s.}}'.replace('%s',v))}
if(r=='läuft …'||r=='noch nicht geprüft')say('{{Suche nach einer neuen Version …}}');
else if(r.startsWith('Installation')||r.startsWith('neue Version')||r.includes('installiert'))
say('{{Neue Version wird installiert, danach startet das Display neu …}}');
else end(r=='aktuell'?'{{Die Firmware ist aktuell.}}':r)},2000)}}</script>)JS";

static void handleSettings() {
  if (web.method() == HTTP_POST) {
    // Nur vollständige Formulare übernehmen, sonst stünden leere Felder als 0 in den Einstellungen
    static const struct {
      const char* key;
      const char* label;
    } REQUIRED[] = {
        {"host", TR_KEY("evcc-Adresse")},
                    {"port", TR_KEY("Port")},
                    {"lp", TR_KEY("Ladepunkte")},
                    {"rot", TR_KEY("Drehung")},
                    {"theme", TR_KEY("Design")},
                    {"timeout", TR_KEY("Bildschirm ausschalten")},
                    {"autoupd", TR_KEY("Automatische Updates")}};
    for (const auto& r : REQUIRED) {
      if (web.arg(r.key).isEmpty()) {
        web.send(400, "text/html; charset=utf-8",
                 page(SETTINGS_HEAD) + "<p class=\"note\">" +
                     fmt(tr("Nicht gespeichert: Das Feld „%s“ ist leer."), tr(r.label)) +
                     "</p><a class=\"plain\" href=\"javascript:history.back()\">" + tr("Zurück zum Formular") +
                     "</a></body></html>");
        return;
      }
    }
    updater::confirm();  // wer hier speichert, erreicht das Display – die Firmware läuft also
    // „Solarseite einrichten“ mit offenen Änderungen: erst speichern, dann weiter zur Seite
    const bool toSolar = web.arg("next") == "solar";
    const String oldHostname = settings::get().hostname;
    if (!applySettings([](const char* key) { return web.arg(key); })) {
      // Wirkt sofort: neu zeichnen und zurück zur Seite, die dann die neue Anzeige lädt
      lastActivity = millis();
      dirty = true;
      web.sendHeader("Location", toSolar ? "/solar?saved=2" : "/settings?saved=1");
      web.send(303);
      return;
    }
    restartAt = millis() + 2000;  // Antwortseite noch ausliefern, dann neu starten
    String next = toSolar ? "/solar" : "/settings";
    String note = tr("Gespeichert. Das Display startet neu, die Seite lädt in einigen Sekunden wieder.");
    // Neuer Name: Wer die Seite über den alten .local-Namen aufgerufen hat, landet sonst im Leeren
    const String& hostname = settings::get().hostname;
    if (hostname != oldHostname) {
      String via = web.hostHeader();
      via.toLowerCase();
      const int colon = via.indexOf(':');
      if (colon >= 0) via.remove(colon);
      const String address = "http://" + hostname + ".local:8080";
      if (via.endsWith(".local")) next = address + next;
      note += " " + fmt(tr("Danach ist es unter %s erreichbar."),
                        ("<a href=\"" + address + "/settings\">" + address + "</a>").c_str());
    }
    web.sendHeader("Refresh", "15; url=" + next);
    // Noch in der bisherigen Sprache: eine neu gewählte gilt erst nach dem Neustart
    web.send(200, "text/html; charset=utf-8",
             page(SETTINGS_HEAD) + "<p class=\"note\">" + note + "</p></body></html>");
    return;
  }

  const Settings& s = settings::get();
  String h = page(SETTINGS_HEAD);
  h.reserve(12288);
  if (web.hasArg("saved"))
    h += String("<p class=\"note\">") + tr("Gespeichert – die Änderung ist schon am Display zu sehen.") + "</p>";
  // autocomplete=off: Nach „Zurück“ im Browser steht der gespeicherte Stand da, nicht eine verworfene Eingabe
  h += String("<form method=\"post\" autocomplete=\"off\"><h2>") + tr("Verbindung") + "</h2>";
  // Steht die Verbindung? Das zeigt sofort, ob Adresse und Port stimmen.
  const evcc::Diagnostics d = evcc::diagnostics();
  const uint32_t age = d.lastOkMs ? (millis() - d.lastOkMs) / 1000 : 0;
  if (d.lastOkMs && age < 60) {
    h += String("<p class=\"st\">") + tr("Verbunden mit evcc") + (d.version[0] ? String(" ") + d.version : String()) + "</p>";
  } else if (d.lastOkMs) {
    h += "<p class=\"st bad\">" +
         (age < 120 ? fmt(tr("evcc antwortet nicht mehr, zuletzt vor %lu Sekunden. Läuft evcc, stimmen Adresse und Port?"),
                          (unsigned long)age)
                    : fmt(tr("evcc antwortet nicht mehr, zuletzt vor %lu Minuten. Läuft evcc, stimmen Adresse und Port?"),
                          (unsigned long)(age / 60))) +
         "</p>";
  } else {
    h += String("<p class=\"st bad\">") + tr("Noch keine Antwort von evcc. Stimmen Adresse und Port?") + "</p>";
  }
  h += String("<div class=\"row\"><div><label for=\"host\">") + tr("evcc-Adresse") +
       "</label><input id=\"host\" name=\"host\" required "
       "autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\" value=\"" + htmlEscape(s.evccHost) + "\"></div>";
  h += String("<div style=\"flex:.45\"><label for=\"port\">") + tr("Port") +
       "</label><input id=\"port\" name=\"port\" type=\"number\" required "
       "min=\"1\" max=\"65535\" value=\"" + String(s.evccPort) + "\"></div></div>";
  h += field("lp", tr("Ladepunkte"),
             tr("Nummer wie in evcc, 1 ist der erste. Mehrere mit Komma: 1,2,3 – am Display wechselt Wischen.")) +
       "<input id=\"lp\" name=\"lp\" required inputmode=\"numeric\" pattern=\"[0-9 ,]*\" value=\"" +
       settings::loadpointsText(s) + "\">";
  for (uint8_t i = 0; i < evcc::count(); i++) {
    if (!states[i].valid && states[i].missing)
      h += "<p class=\"st bad\">" + fmt(tr("Ladepunkt %u gibt es in evcc nicht."), (unsigned)s.loadpoints[i]) + "</p>";
  }
  h += String("<p class=\"hint\">") + tr("WLAN wechseln: am Display den Titel 3 Sekunden gedrückt halten.") + "</p>";

  h += String("<h2>") + tr("Ladepunktseite") + "</h2>";
  h += toggle("vehsw", s.vehicleSwitch, tr("Fahrzeug wechseln"),
              tr("Die Fahrzeugzeile antippen schaltet durch die Fahrzeuge aus evcc."));
  h += toggle("limit", s.chargeLimit, tr("Ladelimit"),
              tr("Steht neben dem Fahrzeug; antippen stellt es für diese Ladung ein."));
  h += toggle("boost", s.boostButton, "Battery Boost",
              tr("Im Modus Smart die Akku-Anzeige antippen, um den Hausakku dazuzuschalten."));

  h += String("<h2>") + tr("Solarseite") + "</h2>";
  h += toggle("solar", s.solarPage, tr("Solarseite anzeigen"),
              tr("Eigene Seite mit Erzeugung, Tagesverlauf und Prognose, erreichbar durch Wischen. Den Tagesverlauf "
                 "liefert evcc ab Version 0.319."));
  h += "<fieldset id=\"solarsub\">";
  h += String("<label>") + tr("Inhalt") + "<small>" + tr("Große Zahl, Diagramm und Kacheln selbst auswählen.") +
       "</small></label><a class=\"plain\" href=\"/solar\" id=\"tosolar\">" + tr("Solarseite einrichten") + "</a>";
  h += toggle("solar1st", s.solarFirst, tr("Als Startseite"),
              tr("Etwa für ein Display im Haus: Es bleibt auf der Solarseite, auch wenn ein Auto angesteckt wird. "
                 "Bei Bildschirm-Timeout „nie“ bleibt die gewählte Seite stehen."));
  h += "</fieldset>";

  h += String("<h2>") + tr("Bildschirm") + "</h2><figure><img id=\"shot\" alt=\"" + tr("Aktuelle Anzeige des Displays") +
       "\" width=\"480\" height=\"480\" hidden><button type=\"button\" class=\"plain\" id=\"load\">" +
       tr("Aktuelle Anzeige laden") + "</button></figure>";
  h += field("bright", tr("Helligkeit")) + "<select id=\"bright\" name=\"bright\">";
  for (int pct = 100; pct >= MIN_BRIGHTNESS_PCT; pct -= 10) h += option(pct, s.brightnessPct, fmt(tr("%d %%"), pct).c_str());
  h += "</select>";
  // Manche Boards dimmen nur mit langsamer Taktung (#21). Sie gilt ab dem Start, deshalb der Neustart.
  const bool slowBacklight = s.backlightHz != LCD_BACKLIGHT_HZ;
  h += toggle("slowbl", slowBacklight, tr("Dimmen mit langsamer Taktung"),
              (String(tr("Nur einschalten, wenn der Bildschirm unter 90&nbsp;% Helligkeit dunkel bleibt oder flackert.")) +
               (slowBacklight && s.backlightHz != SLOW_BACKLIGHT_HZ
                    ? fmt(tr(" Eingestellt sind %lu Hz."), (unsigned long)s.backlightHz)
                    : String()))
                  .c_str());
  h += field("timeout", tr("Bildschirm ausschalten"), tr("Solange kein Auto lädt und niemand das Display bedient.")) +
       "<select id=\"timeout\" name=\"timeout\">";
  bool listed = false;
  for (int minutes : {0, 1, 2, 5, 10, 15, 30, 60, 120, 240}) {
    // Ein früher frei eingegebener Wert bleibt wählbar und steht an seinem Platz in der Reihe
    if (!listed && s.screenTimeoutMin < minutes)
      h += option(s.screenTimeoutMin, s.screenTimeoutMin, timeoutLabel(s.screenTimeoutMin).c_str());
    if (s.screenTimeoutMin <= minutes) listed = true;
    h += option(minutes, s.screenTimeoutMin, timeoutLabel(minutes).c_str());
  }
  h += "</select>";
  h += field("theme", tr("Design")) + "<select id=\"theme\" name=\"theme\">" +
       option(0, s.theme, tr("Automatisch: tagsüber hell, nachts dunkel")) + option(1, s.theme, tr("Immer hell")) +
       option(2, s.theme, tr("Immer dunkel")) + "</select>";
  char lat[12], lon[12];
  snprintf(lat, sizeof(lat), "%.4f", s.latitude);
  snprintf(lon, sizeof(lon), "%.4f", s.longitude);
  h += String("<div id=\"loc\"><label>") + tr("Standort") + "<small>" + tr("Für Sonnenauf- und -untergang, grob genügt.") +
       "</small></label><div class=\"row\" style=\"margin-top:.5rem\">"
       "<div><label class=\"mini\" for=\"lat\">" + tr("Breitengrad") +
       "</label><input id=\"lat\" name=\"lat\" type=\"number\" step=\"any\" min=\"-90\" "
       "max=\"90\" value=\"" + String(lat) + "\"></div>"
       "<div><label class=\"mini\" for=\"lon\">" + tr("Längengrad") +
       "</label><input id=\"lon\" name=\"lon\" type=\"number\" step=\"any\" min=\"-180\" "
       "max=\"180\" value=\"" + String(lon) + "\"></div></div></div>";
  h += field("rot", tr("Drehung")) + "<select id=\"rot\" name=\"rot\">" + option(0, s.rotation, "0°") +
       option(1, s.rotation, tr("90° im Uhrzeigersinn")) + option(2, s.rotation, "180°") +
       option(3, s.rotation, tr("270° im Uhrzeigersinn")) + "</select>";

  h += "<h2>System</h2>";
  // In beiden Sprachen beschriftet, damit die Auswahl auch findet, wer die eingestellte Sprache nicht liest
  h += field("lang", "Sprache / Language") + "<select id=\"lang\" name=\"lang\">" +
       option((int)Lang::De, s.language, "Deutsch") + option((int)Lang::En, s.language, "English") + "</select>";
  h += field("name", tr("Name im Heimnetz"),
             fmt(tr("Das Display ist unter %s erreichbar. Mehrere Displays brauchen verschiedene Namen, z. B. "
                    "evcc-kueche. Erlaubt sind a–z, 0–9 und Bindestrich."),
                 ("http://" + s.hostname + ".local:8080").c_str())
                 .c_str()) +
       "<input id=\"name\" name=\"name\" required maxlength=\"" + String(MAX_HOSTNAME_LEN) +
       "\" autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\" "
       "value=\"" + htmlEscape(s.hostname) + "\">";
  h += toggle("autoupd", s.autoUpdate, tr("Automatische Updates"),
              tr("Installiert neue Versionen, sobald das Display zehn Minuten nicht gebraucht wird und kein Auto lädt."));
  h += field("pw", tr("Passwort"),
             (fmt(tr("Schützt diese Seite und Firmware-Uploads im Heimnetz, Benutzername „%s“. "), WEB_USER) +
              (s.password.isEmpty() ? tr("Aktuell ist keines gesetzt.")
                                    : tr("Aktuell ist eines gesetzt; leer lassen, um es zu behalten.")) +
              tr(" Vergessen? Speichern im WLAN-Portal am Display löscht es."))
                 .c_str()) +
       "<input id=\"pw\" name=\"pw\" type=\"password\" autocomplete=\"new-password\" maxlength=\"" + String(MAX_PASSWORD_LEN) +
       "\" placeholder=\"" + (s.password.isEmpty() ? tr("Kein Passwort") : tr("Unverändert")) + "\">"
       "<label class=\"check\"><input type=\"checkbox\" id=\"pwshow\">" + tr("Passwort anzeigen") + "</label>";
  if (!s.password.isEmpty())
    h += String("<label class=\"check\"><input type=\"checkbox\" name=\"pwclear\" value=\"1\">") + tr("Passwort entfernen") +
         "</label>";
  // Die Leiste bleibt am unteren Rand stehen, solange das Formular zu sehen ist
  h += String("<div class=\"bar\"><small id=\"dirty\" hidden>") + tr("Ungespeicherte Änderungen") +
       " · <a href=\"#\" id=\"undo\">" + tr("Verwerfen") + "</a></small><button>" + tr("Speichern") + "</button></div></form>";

  h += "<h2>Firmware</h2><p class=\"hint\">" +
       fmt(tr("Installiert ist Version %s. Beim Aktualisieren startet das Display neu, eine laufende Ladung läuft weiter."),
           "<span id=\"fwv\">" FW_VERSION "</span>") +
       "</p><button type=\"button\" class=\"plain\" id=\"upd\">" + tr("Jetzt aktualisieren") +
       "</button><p class=\"hint\" id=\"updmsg\" hidden></p><p class=\"hint\">" +
       fmt(tr("Bleibt das Display auf einer alten Version stehen, hilft die %sAnleitung für Updates von Hand%s."),
           "<a href=\"" PROJECT_URL "hilfe.html#update-von-hand\" target=\"_blank\" rel=\"noopener\">", "</a>") +
       "</p>";
  h += String("<h2>") + tr("Diagnose") + "</h2><p class=\"hint\">" +
       fmt(tr("Für die Fehlersuche. Die Datei enthält keine Passwörter, WLAN-Namen, Netzwerkadressen, Standorte oder "
              "Namen von Ladepunkten und Fahrzeugen und kann so an ein %sIssue auf GitHub%s angehängt werden."),
           "<a href=\"" REPO_URL "/issues\" target=\"_blank\" rel=\"noopener\">", "</a>") +
       "</p><a class=\"plain\" href=\"/diag?download=1\" download>" + tr("Diagnose herunterladen") +
       "</a><a class=\"plain\" href=\"/diag\" target=\"_blank\">" + tr("Ansehen") + "</a>";
  h += page(SETTINGS_SCRIPT);
  h += page(UPDATE_SCRIPT);
  h += String("<footer><a href=\"" PROJECT_URL "\" target=\"_blank\" rel=\"noopener\">") + tr("Projektseite und Anleitung") +
       "</a> · <a href=\"" PROJECT_URL "hilfe.html\" target=\"_blank\" rel=\"noopener\">" + tr("Hilfe") +
       "</a> · <a href=\"" REPO_URL "\" target=\"_blank\" rel=\"noopener\">GitHub</a></footer></body></html>";
  web.send(200, "text/html; charset=utf-8", h);
}

// ---------- Solarseite einrichten (/solar) ----------
// Die Vorschau ist eine nachgebaute Solarseite und reagiert sofort; das echte Bild vom Display lädt ein Knopf.
// Das Skript bekommt den Stand als Objekt C: Auswahl, was evcc liefert, und die aktuellen Werte.
static const char SOLAR_EDITOR_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="{{@lang}}"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>{{Solarseite einrichten}} · evcc-Garage</title><style>
:root{color-scheme:light dark;--a:#0fdd8d}[hidden]{display:none!important}
body{font:16px/1.4 system-ui,sans-serif;max-width:46rem;margin:0 auto;padding:1.5rem 1rem}
.back{display:inline-block;font-size:.9rem;opacity:.65;color:inherit;text-decoration:none;padding:.4rem 0;margin-bottom:.2rem}
h1{font-size:1.4rem;margin:0 0 .3rem}.lead{margin:0 0 1.3rem;opacity:.65}
.note{padding:.6rem .8rem;border-radius:.5rem;background:#0fdd8d33;margin:0 0 1.2rem}.note a{color:inherit}
.wrap{display:grid;grid-template-columns:minmax(0,19rem) minmax(0,1fr);gap:1.75rem;align-items:start}
.left{position:sticky;top:.75rem}
@media(max-width:620px){.wrap{grid-template-columns:1fr;gap:1.2rem}.left{position:static;max-width:22rem;margin:0 auto;width:100%}}
.screen{position:relative;aspect-ratio:1;border-radius:1.1rem;background:#010322;overflow:hidden;border:1px solid #8886}
.screen img{position:absolute;inset:0;width:100%;height:100%}
.s480{position:absolute;left:0;top:0;width:480px;height:480px;transform-origin:0 0;font-family:Montserrat,system-ui,sans-serif;color:#fff}
.hd{position:absolute;left:28px;right:28px;top:20px;display:flex;justify-content:space-between;align-items:center;font-weight:700;font-size:26px}
.hd .r{color:#B5B6C3}
.bd{position:absolute;left:24px;right:24px;top:62px;bottom:40px;display:flex;flex-direction:column}
.area{all:unset;box-sizing:border-box;cursor:pointer;border-radius:16px;outline:3px solid transparent;outline-offset:3px}
.area:hover{outline-color:#ffffff3d}.area.sel{outline-color:var(--a)}.area:focus-visible{outline-color:#fff}
.big{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:0}
.big .lb{font-size:22px;font-weight:500;color:#B5B6C3;margin-bottom:10px}
.big .nr{display:flex;align-items:baseline;gap:14px}
.big .v{font-size:122px;font-weight:700;line-height:.8;letter-spacing:-2px}
.big .u{font-size:36px;font-weight:700;color:#B5B6C3}
.big svg{align-self:center;width:62px;height:62px;color:#0FDE41;margin-left:2px}
.mid{display:flex;flex-direction:column;margin-top:8px}
.flow{height:40px;border-radius:20px;display:flex;overflow:hidden;margin-bottom:16px}
.flow .g{flex:93;background:#0FDE41}.flow .y{flex:7;background:#FAF000}
.chart{display:flex;align-items:flex-end;gap:7px}.chart .c{flex:1;position:relative;height:100%}
.chart .f,.chart .e{position:absolute;left:0;right:0;bottom:0;border-radius:6px}
.chart .f{background:#33354F}.chart .e{background:#0FDE41}
.chart .z{position:absolute;left:0;right:0;bottom:0;height:4px;border-radius:2px;background:#2A2C4C}
.axis{display:flex;gap:7px;margin-top:6px;height:20px;font-size:15px;font-weight:500;color:#B5B6C3}
.axis span{flex:1;text-align:center;white-space:nowrap}
.plants{display:flex;flex-direction:column;gap:14px;padding:6px 4px 12px}
.plants .p{display:grid;grid-template-columns:1fr auto;row-gap:7px;font-size:22px;font-weight:500}.plants .p b{font-weight:700}
.plants .t{grid-column:1/3;height:10px;border-radius:5px;background:#2A2C4C;overflow:hidden}
.plants .t i{display:block;height:100%;border-radius:5px;background:#0FDE41}
.tiles{display:flex;gap:12px;margin-top:12px}
.tile{flex:1;min-width:0;background:#151630;border-radius:14px;padding:9px 14px 10px;text-align:left}
.tile .l{display:block;font-size:15px;font-weight:500;color:#B5B6C3;white-space:nowrap}
.tile .w{display:block;font-size:22px;font-weight:700;white-space:nowrap;margin-top:1px}
.tile.empty{flex:0 0 46px;background:none;border:2px dashed #33354F;display:flex;align-items:center;justify-content:center;color:#93949E;font-size:26px;padding:0}
.dots{position:absolute;left:0;right:0;bottom:13px;display:flex;justify-content:center;gap:12px}
.dots i{width:10px;height:10px;border-radius:50%;background:#55576F}.dots i.on{background:#fff}
.cap{font-size:.85rem;opacity:.65;margin:.6rem 0 .5rem;text-align:center}.cen{text-align:center}
.slot{border:1px solid #8886;border-radius:.6rem;margin-bottom:.6rem;overflow:hidden}.slot.open{border-color:var(--a)}
.sh{all:unset;box-sizing:border-box;cursor:pointer;display:flex;width:100%;min-height:2.75rem;justify-content:space-between;align-items:center;gap:.75rem;padding:.7rem .9rem}
.sh:focus-visible{outline:2px solid var(--a);outline-offset:-2px;border-radius:.6rem}
.sh b{font-weight:600}.sh span{opacity:.65;text-align:right}
.sh span::after{content:"›";display:inline-block;margin-left:.6rem;transform:rotate(90deg)}
.slot.open .sh span::after{transform:rotate(-90deg)}
.opts{display:none;padding:0 .5rem .5rem}.slot.open .opts{display:block}
.opt{display:flex;align-items:center;gap:.7rem;min-height:2.75rem;box-sizing:border-box;padding:.5rem .55rem;border-radius:.45rem;cursor:pointer}
.opt:hover{background:#8881}.opt.off{opacity:.45;cursor:default}
.opt input{appearance:none;flex:none;width:1.15rem;height:1.15rem;margin:0;border-radius:50%;border:2px solid #8889;display:grid;place-content:center}
.opt input:checked{border-color:var(--a);background:var(--a)}
.opt input:checked::after{content:"";width:.42rem;height:.42rem;border-radius:50%;background:#000}
.opt input:focus-visible{outline:2px solid var(--a);outline-offset:2px}
.opt .n{flex:1;min-width:0}.opt small{display:block;opacity:.65;font-size:.85rem}
.opt .x{opacity:.65;font-size:.9rem;white-space:nowrap}.opt:has(input:checked){background:#0fdd8d24}
.bar{position:sticky;bottom:0;margin-top:1.1rem;padding:.75rem 0;background:Canvas}
.bar small{display:block;margin-bottom:.5rem;font-weight:600;font-size:.85rem}.bar a{color:inherit;font-weight:400}
.act{display:flex;gap:.6rem;flex-wrap:wrap}
button{font:inherit;font-weight:600;min-height:2.75rem;padding:.75rem 1.2rem;border:0;border-radius:.5rem;background:var(--a);color:#000;cursor:pointer;flex:1}
button.plain{flex:none;background:none;border:1px solid #8888;color:inherit;font-weight:400;padding:.5rem .9rem}
</style></head><body>
<a class="back" href="/settings" id="back">‹ {{Einstellungen}}</a>
<p class="note" id="leave" hidden>{{Die Änderungen an der Solarseite sind noch nicht gespeichert.}}
<span class="act" style="margin-top:.6rem"><button type="button" id="lsave">{{Speichern und zurück}}</button>
<button type="button" class="plain" id="ldrop">{{Verwerfen und zurück}}</button></span></p>
<h1>{{Solarseite einrichten}}</h1>
<p class="lead">{{Im Bild einen Bereich antippen und auswählen, was dort stehen soll.}}</p>
<p class="note" id="saved" hidden>{{Gespeichert – die Solarseite sieht am Display jetzt so aus.}}</p>
<p class="note" id="off" hidden>{{Die Solarseite ist ausgeschaltet. Einschalten lässt sie sich in den <a href="/settings">Einstellungen</a> unter „Solarseite“.}}</p>
<form method="post" class="wrap" autocomplete="off">
<div class="left"><div class="screen" id="screen"><div class="s480" id="s480">
<div class="hd"><span>Solar</span><span class="r" id="clock"></span></div>
<div class="bd"><button type="button" class="area big" data-slot="big" id="aBig"></button>
<button type="button" class="area mid" data-slot="mid" id="aMid"></button><div class="tiles" id="aTiles"></div></div>
<div class="dots"><i></i><i class="on"></i></div></div><img id="shot" alt="{{Echtes Bild vom Display}}" hidden></div>
<p class="cap" id="cap"></p><p class="cen"><button type="button" class="plain" id="real">{{Echtes Bild vom Display laden}}</button></p></div>
<div><div id="slots"></div>
<div class="bar"><small id="dirty" hidden>{{Ungespeicherte Änderungen}} · <a href="#" id="undo">{{Verwerfen}}</a></small>
<div class="act"><button>{{Speichern}}</button><button type="button" class="plain" id="reset">{{Zurück auf Standard}}</button></div></div>
<input type="hidden" name="big"><input type="hidden" name="mid"><input type="hidden" name="t1"><input type="hidden" name="t2"><input type="hidden" name="t3">
</div></form>
<script>const C=)HTML";

static const char SOLAR_EDITOR_TAIL[] PROGMEM = R"HTML(;
{const $=i=>document.getElementById(i),f=document.forms[0];
const I=p=>'<svg viewBox="0 0 64 64" fill="none" stroke="currentColor" stroke-width="5" stroke-linejoin="round" stroke-linecap="round">'+p+'</svg>';
const SUN=I('<circle cx="32" cy="32" r="11"/><circle cx="32" cy="32" r="25" stroke-width="8" stroke-dasharray="6.2 6.9" stroke-linecap="butt"/>'),
HOUSE=I('<path d="M8 30 32 9l24 21"/><path d="M15 26v28h34V26"/><path d="M27 54V38h10v16"/>'),
GRID=I('<path d="M24 58 29 8h6l5 50"/><path d="M12 22h40M17 34h30"/>'),
BAT=I('<rect x="8" y="20" width="42" height="26" rx="6"/><path d="M57 29v8"/><path d="M17 29h18v8H17z" fill="currentColor"/>');
// Werte von jetzt; ohne Daten von evcc Beispielwerte
const V=C.v||{pv:4400,sur:3500,home:900,grid:-3000,batp:-500,soc:78,today:18.2,rest:4.3,tom:19,fc:22.5};
const kw=w=>(Math.abs(w)/1000).toFixed(1).replace('.','{{@dec}}'),kwh=e=>e.toFixed(1).replace('.','{{@dec}}')+' kWh';
const no={bat:'{{evcc meldet keinen Hausakku}}',grid:'{{evcc meldet keine Netzleistung}}',plants:'{{nur bei mehreren PV-Anlagen in evcc}}',
hist:'{{braucht evcc ab Version 0.319}}',fc:'{{evcc hat keine Solarprognose}}'};
const BIG=[{n:'{{Erzeugung}}',d:'{{Was die PV-Anlage gerade liefert}}',lb:'{{Erzeugung jetzt}}',v:kw(V.pv),ic:SUN},
{n:'{{Überschuss}}',d:'{{Was gerade übrig ist, etwa fürs Auto}}',lb:'{{Überschuss jetzt}}',v:kw(V.sur),ic:SUN},
{n:'{{Verbrauch}}',d:'{{Verbrauch im Haus ohne Ladepunkte}}',lb:'{{Verbrauch jetzt}}',v:kw(V.home),ic:HOUSE},
{n:'{{Netz}}',d:'{{Einspeisung oder Netzbezug, je nach Richtung}}',lb:V.grid>0?'{{Netzbezug jetzt}}':'{{Einspeisung jetzt}}',v:kw(V.grid),ic:GRID,need:'grid'},
{n:'{{Hausakku}}',d:'{{Leistung, mit der der Akku lädt oder entlädt}}',lb:V.batp>0?'{{Akku entlädt mit}}':'{{Akku lädt mit}}',v:kw(V.batp),ic:BAT,need:'bat'}];
const MID=[null,{n:'{{Tagesverlauf}}',d:'{{Erzeugung je Stunde, dahinter die Prognose}}'},
{n:'{{Energiebalken und Tagesverlauf}}',d:'{{Balken wie auf der Ladepunktseite: Netz, Sonne, Akku}}',need:'grid'},
{n:'{{Anlagen einzeln}}',d:'{{Leistung je PV-Anlage}}',need:'plants'}];
const TILE=[{n:'{{Heute erzeugt}}',l:'{{HEUTE}}',w:kwh(V.today),need:'hist'},
{n:'{{Rest heute}}',d:'{{Prognose für den Rest des Tages; entfällt abends}}',l:'{{REST HEUTE}}',w:kwh(V.rest),need:'fc'},
{n:'{{Morgen}}',d:'{{Prognose für morgen}}',l:'{{MORGEN}}',w:kwh(V.tom),need:'fc'},
{n:'{{Prognose heute}}',d:'{{Prognose für den ganzen Tag}}',l:'{{PROGNOSE}}',w:kwh(V.fc),need:'hist'},
{n:'{{Erzeugung}}',d:'{{jetzt}}',l:'{{ERZEUGUNG}}',w:kw(V.pv)+' kW'},{n:'{{Überschuss}}',d:'{{jetzt}}',l:'{{ÜBERSCHUSS}}',w:kw(V.sur)+' kW'},
{n:'{{Verbrauch}}',d:'{{jetzt, im Haus}}',l:'{{VERBRAUCH}}',w:kw(V.home)+' kW'},
{n:'{{Netz}}',d:'{{jetzt, Einspeisung oder Bezug}}',l:V.grid>0?'{{AUS NETZ}}':'{{INS NETZ}}',w:kw(V.grid)+' kW',need:'grid'},
{n:'{{Hausakku-Ladestand}}',d:'{{in Prozent}}',l:'{{AKKU}}',w:Math.round(V.soc)+'{{@pct}}',need:'bat'},
{n:'{{Keine Kachel}}',d:'{{Die anderen Kacheln werden breiter}}',l:'',w:'–'},
{n:'{{Hausakku-Leistung}}',d:'{{jetzt, beim Laden oder Entladen}}',l:V.batp<=-50?'{{IN AKKU}}':V.batp>=50?'{{AUS AKKU}}':'{{AKKU}}',w:kw(V.batp)+' kW',need:'bat'}];
// Reihenfolge in der Auswahl; die Nummern stehen so im Speicher des Displays
const TORD=[0,1,2,3,4,5,6,7,8,10,9];
const SLOTS=[{id:'big',t:'{{Große Zahl}}',o:BIG},{id:'mid',t:'{{Diagramm}}',o:MID},{id:'t1',t:'{{Kachel links}}',o:TILE,ord:TORD},
{id:'t2',t:'{{Kachel Mitte}}',o:TILE,ord:TORD},{id:'t3',t:'{{Kachel rechts}}',o:TILE,ord:TORD}];
const DEF={big:0,mid:C.has.plants?3:1,t1:0,t2:1,t3:2},S0={big:C.big,mid:C.mid,t1:C.t[0],t2:C.t[1],t3:C.t[2]};
let st={...S0},cur='big',sub=0;
const E=[0,0,.14,.52,.78,.93,.9,.2,0,0,0,0,0,0,0],F=[0,0,0,0,0,0,0,.6,.42,.2,.05,0,0,0,0];
const ok=o=>!o.need||C.has[o.need];
function draw(){$('shot').hidden=true;
const b=BIG[st.big];$('aBig').innerHTML='<span class="lb">'+b.lb+'</span><span class="nr"><span class="v">'+b.v+'</span><span class="u">kW</span>'+b.ic+'</span>';
let m='';
if(st.mid==3)m='<div class="plants"><div class="p"><span>{{Anlage 1}}</span><b>'+kw(V.pv*.66)+' kW</b><span class="t"><i style="width:66%"></i></span></div>'+
'<div class="p"><span>{{Anlage 2}}</span><b>'+kw(V.pv*.34)+' kW</b><span class="t"><i style="width:34%"></i></span></div></div>';
else{if(st.mid==2)m+='<div class="flow"><span class="g"></span><span class="y"></span></div>';
m+='<div class="chart" style="height:'+(st.mid==2?78:108)+'px">'+E.map((e,i)=>'<span class="c">'+(e||F[i]?'':'<span class="z"></span>')+
(F[i]?'<span class="f" style="height:'+F[i]*100+'%"></span>':'')+(e?'<span class="e" style="height:'+e*100+'%"></span>':'')+'</span>').join('')+
'</div><div class="axis">'+E.map((e,i)=>'<span>'+(i%3?'':i+6)+'</span>').join('')+'</div>'}
$('aMid').innerHTML=m;
const three=![st.t1,st.t2,st.t3].includes(9);
$('aTiles').innerHTML=['t1','t2','t3'].map(k=>{const t={...TILE[st[k]]};if(three&&st[k]==5)t.l='{{ÜBRIG}}';return st[k]==9
?'<button type="button" class="area tile empty" data-slot="'+k+'" aria-label="{{Leere Kachel}}">+</button>'
:'<button type="button" class="area tile" data-slot="'+k+'"><span class="l">'+t.l+'</span><span class="w">'+t.w+'</span></button>'}).join('');
document.querySelectorAll('.area').forEach(a=>{a.classList.toggle('sel',a.dataset.slot==cur);a.onclick=()=>pick(a.dataset.slot,1)});
for(const k in st)f.elements[k].value=st[k];
const d=Object.keys(st).some(k=>st[k]!=S0[k]);$('dirty').hidden=!d;if(!d)$('leave').hidden=true}
function list(){$('slots').innerHTML=SLOTS.map(s=>'<section class="slot'+(s.id==cur?' open':'')+'" id="s_'+s.id+'"><button type="button" class="sh" data-slot="'+s.id+
'" aria-expanded="'+(s.id==cur)+'"><b>'+s.t+'</b><span>'+s.o[st[s.id]].n+'</span></button><div class="opts">'+(s.ord||s.o.map((o,k)=>k)).map(k=>{const o=s.o[k];return !o?'':
'<label class="opt'+(ok(o)?'':' off')+'"><input type="radio" name="r_'+s.id+'" value="'+k+'"'+(st[s.id]==k?' checked':'')+(ok(o)||st[s.id]==k?'':' disabled')+
'><span class="n">'+o.n+'<small>'+(ok(o)?o.d||'':'{{Nicht verfügbar: %s}}'.replace('%s',no[o.need]))+'</small></span>'+(o.w||o.v?'<span class="x">'+(o.w||o.v+' kW')+'</span>':'')+'</label>'}).join('')+
'</div></section>').join('');
document.querySelectorAll('.sh').forEach(b=>b.onclick=()=>pick(b.dataset.slot==cur?'':b.dataset.slot));
document.querySelectorAll('.opt input').forEach(i=>i.onchange=()=>{const k=i.name.slice(2);st[k]=+i.value;draw();
i.closest('.slot').querySelector('.sh span').textContent=SLOTS.find(s=>s.id==k).o[st[k]].n})}
function pick(id,scroll){cur=id;list();draw();
if(scroll&&id&&matchMedia('(max-width:620px)').matches)$('s_'+id).scrollIntoView({behavior:'smooth',block:'nearest'})}
$('reset').onclick=()=>{st={...DEF};list();draw()};
$('undo').onclick=e=>{e.preventDefault();st={...S0};list();draw()};
f.onsubmit=()=>{sub=1};
// Zurück mit offenen Änderungen: auf der Seite fragen statt mit der Meldung des Browsers
document.querySelectorAll('a[href="/settings"]').forEach(a=>a.onclick=e=>{if($('dirty').hidden)return;e.preventDefault();$('leave').hidden=false;$('lsave').focus();$('leave').scrollIntoView({block:'nearest'})});
$('lsave').onclick=()=>{const n=document.createElement('input');n.type='hidden';n.name='next';n.value='settings';f.append(n);sub=1;f.submit()};
$('ldrop').onclick=()=>{sub=1;location.href='/settings'};
addEventListener('beforeunload',e=>{if(!$('dirty').hidden&&!sub){e.preventDefault();e.returnValue=''}});
// Das Display zeigt die Auswahl eine Minute lang mit den echten Daten, dann holt die Seite sein Bild
$('real').onclick=async()=>{const b=$('real'),i=$('shot');b.disabled=true;b.textContent='{{Lädt …}}';
const done=t=>{b.disabled=false;b.textContent=t};
try{await fetch('/preview?s=pvlive&cfg='+[st.big,st.mid,st.t1,st.t2,st.t3].join(','));await new Promise(r=>setTimeout(r,1500));
i.onload=()=>{i.hidden=false;done('{{Echtes Bild neu laden}}')};i.onerror=()=>done('{{Erneut versuchen}}');i.src='/screenshot.bmp?'+Date.now()}
catch(e){done('{{Erneut versuchen}}')}};
$('clock').textContent=new Date().toLocaleTimeString('{{@locale}}',{hour:'2-digit',minute:'2-digit'});
$('cap').textContent=C.v?'{{Vorschau mit den Werten von jetzt, Diagramm als Beispiel}}':'{{Vorschau mit Beispielwerten}}';
$('saved').hidden=!C.saved;if(C.saved==2)$('saved').textContent='{{Einstellungen gespeichert.}}';$('off').hidden=C.on;if(C.saved)history.replaceState(null,'','/solar');
new ResizeObserver(()=>{$('s480').style.transform='scale('+$('screen').clientWidth/480+')'}).observe($('screen'));
list();draw()}
</script></body></html>)HTML";

static void handleSolarEditor() {
  Settings& st = settings::get();
  if (web.method() == HTTP_POST) {
    st.solarBig = constrain(web.arg("big").toInt(), 0L, (long)SOLAR_BIG_COUNT - 1);
    st.solarMid = constrain(web.arg("mid").toInt(), 1L, (long)SOLAR_MID_COUNT - 1);
    for (int i = 0; i < 3; i++)
      st.solarTile[i] = constrain(web.arg(String("t") + (i + 1)).toInt(), 0L, (long)SOLAR_TILE_COUNT - 1);
    settings::save();
    updater::confirm();  // wer hier speichert, erreicht das Display – die Firmware läuft also
    lastActivity = millis();
    dirty = true;
    web.sendHeader("Location", web.arg("next") == "settings" ? "/settings?saved=1" : "/solar?saved=1");
    web.send(303);
    return;
  }

  const EvccState& s = states[0];  // die Hausdaten stehen in jedem Ladepunkt
  const bool data = s.valid && s.pvPower >= 0;
  // "Wie früher" zeigt die Seite als das, was das Display daraus macht
  uint8_t mid = st.solarMid;
  if (mid == SOLAR_MID_AUTO)
    mid = st.solarBig == SOLAR_BIG_SURPLUS ? SOLAR_MID_FLOW : solar.plantCount >= 2 ? SOLAR_MID_PLANTS : SOLAR_MID_CHART;
  char values[200] = "null";
  if (data)
    snprintf(values, sizeof(values),
             "{\"pv\":%.0f,\"sur\":%.0f,\"home\":%.0f,\"grid\":%.0f,\"batp\":%.0f,\"soc\":%.0f,\"today\":%.1f,\"rest\":%.1f,"
             "\"tom\":%.1f,\"fc\":%.1f}",
             fmaxf(0.0f, s.pvPower), ui::surplusPower(s), fmaxf(0.0f, s.homePower), s.gridPower, s.batteryPower,
             fmaxf(0.0f, s.batterySoc), solar.today, fmaxf(0.0f, s.solarToday) / 1000.0f,
             fmaxf(0.0f, s.solarTomorrow) / 1000.0f, solar.forecastToday);
  char json[420];
  // Ohne Daten von evcc ist unbekannt, was es liefert: dann bleibt alles wählbar. Verlauf und Anlagen
  // fragt das Display nur ab, solange die Solarseite eingeschaltet ist.
  snprintf(json, sizeof(json),
           "{\"big\":%u,\"mid\":%u,\"t\":[%u,%u,%u],\"on\":%d,\"saved\":%d,\"has\":{\"bat\":%d,\"grid\":%d,\"plants\":%d,"
           "\"hist\":%d,\"fc\":%d},\"v\":%s}",
           st.solarBig, mid, st.solarTile[0], st.solarTile[1], st.solarTile[2], st.solarPage, (int)web.arg("saved").toInt(),
           !data || s.batterySoc >= 0, !data || s.hasGrid, !data || !st.solarPage || solar.plantCount >= 2,
           !data || !st.solarPage || solar.valid, !data || s.solarToday >= 0, values);
  String h = page(SOLAR_EDITOR_HEAD);
  h.reserve(16384);
  h += json;
  h += page(SOLAR_EDITOR_TAIL);
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
  guarded("/solar", handleSolarEditor);
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
    ArduinoOTA.setHostname(settings::get().hostname.c_str());
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
      ui::showMessage(ICON_SETTINGS, "Update", tr("Neue Firmware wird installiert …"));
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
  ui::showMessage(ICON_SETTINGS, tr("Verbinde …"), tr("WLAN-Verbindung wird aufgebaut"));
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
    char text[200];
    snprintf(text, sizeof(text),
             tr("Mit dem WLAN \"%s\" verbinden. Die Seite öffnet sich automatisch, sonst 192.168.4.1 aufrufen."),
             PORTAL_AP_NAME);
    ui::showMessage(ICON_WIFI_OFF, tr("WLAN einrichten"), text);
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
      snprintf(text, sizeof(text), tr("Version %s wird geladen und installiert …"), updater::availableVersion());
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
    lastActivity = millis();  // Das Portal kann länger offen gewesen sein als die Zeit bis zum Ausschalten
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
