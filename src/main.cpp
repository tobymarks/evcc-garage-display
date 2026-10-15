// evcc-Garagendisplay: zeigt die Ladeleistung eines evcc-Ladepunkts und schaltet
// zwischen PV, Smart und Schnell um. Hardware: Guition ESP32-4848S040C (4", 480x480).

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <lvgl.h>

#include "config.h"
#include "display.h"
#include "evcc_client.h"
#include "icons.h"
#include "preview.h"
#include "settings.h"
#include "sun.h"
#include "ui.h"

static WiFiManager wm;
static WiFiManagerParameter* pHost;
static WiFiManagerParameter* pPort;
static WiFiManagerParameter* pLoadpoint;
static WiFiManagerParameter* pLat;
static WiFiManagerParameter* pLon;
static WiFiManagerParameter* pRotate;
static WiFiManagerParameter* pTheme;
static WiFiManagerParameter* pTimeout;
static WebServer web(8080);  // Port 80 gehört dem Einstellungsportal

static bool portalActive = false;
static bool otaStarted = false;
static bool timeStarted = false;
static bool webStarted = false;
static uint32_t restartAt = 0;

static EvccState state;
static ChargeMode pendingMode = ChargeMode::None;
static uint32_t pendingSince = 0;
static const char* flashMsg = nullptr;
static uint32_t flashUntil = 0;
static bool dirty = true;

static uint32_t lastActivity = 0;
static int brightness = 0;

// Vorschau einzelner Zustände über http://evcc-garage.local/preview?s=…
static EvccState previewState;
static bool previewStale = false;
static int previewDark = -1;  // -1 = Einstellung verwenden
static uint32_t previewUntil = 0;

// ---------- WLAN-Einrichtung / Einstellungen ----------

static void saveParams() {
  Settings& s = settings::get();
  s.evccHost = pHost->getValue();
  s.evccHost.trim();
  s.evccPort = constrain(atoi(pPort->getValue()), 1, 65535);
  s.loadpoint = constrain(atoi(pLoadpoint->getValue()), 1, 9);
  s.latitude = atof(pLat->getValue());
  s.longitude = atof(pLon->getValue());
  s.rotation = constrain(atoi(pRotate->getValue()), 0, 3);
  s.theme = constrain(atoi(pTheme->getValue()), 0, 2);
  s.screenTimeoutMin = constrain(atoi(pTimeout->getValue()), 0, 240);
  settings::save();
  restartAt = millis() + 2000;  // Antwortseite noch ausliefern, dann neu starten
}

static void setupPortal() {
  const Settings& s = settings::get();
  static char port[6], lp[3], lat[12], lon[12], theme[2], rot[2], timeout[4];
  snprintf(port, sizeof(port), "%u", s.evccPort);
  snprintf(lp, sizeof(lp), "%u", s.loadpoint);
  snprintf(lat, sizeof(lat), "%.4f", s.latitude);
  snprintf(lon, sizeof(lon), "%.4f", s.longitude);
  snprintf(theme, sizeof(theme), "%u", s.theme);
  snprintf(rot, sizeof(rot), "%u", s.rotation);
  snprintf(timeout, sizeof(timeout), "%u", s.screenTimeoutMin);

  pHost = new WiFiManagerParameter("host", "evcc-Adresse", s.evccHost.c_str(), 40);
  pPort = new WiFiManagerParameter("port", "evcc-Port", port, 5);
  pLoadpoint = new WiFiManagerParameter("lp", "Ladepunkt-Nr. (1 = erster in evcc)", lp, 2);
  pLat = new WiFiManagerParameter("lat", "Breitengrad (für Tag/Nacht)", lat, 11);
  pLon = new WiFiManagerParameter("lon", "Längengrad (für Tag/Nacht)", lon, 11);
  pRotate = new WiFiManagerParameter("rot", "Drehung: 0–3 (je 90° im Uhrzeigersinn)", rot, 1);
  pTheme = new WiFiManagerParameter("theme", "Design: 0 = automatisch (Sonnenstand), 1 = immer hell, 2 = immer dunkel", theme, 1);
  pTimeout = new WiFiManagerParameter("timeout", "Bildschirm aus nach Minuten ohne Ladung und Bedienung (0 = immer an)", timeout, 3);
  for (auto* p : {pHost, pPort, pLoadpoint, pLat, pLon, pRotate, pTheme, pTimeout}) wm.addParameter(p);

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

static void onModeSelected(ChargeMode mode) {
  lastActivity = millis();
  const ChargeMode current = pendingMode != ChargeMode::None ? pendingMode : state.mode;
  if (mode == current) return;
  pendingMode = mode;
  pendingSince = millis();
  flashMsg = nullptr;
  evcc::requestMode(mode);
  dirty = true;
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

static void updateState() {
  static bool lastConnected = false, lastCharging = false, lastWifi = false, lastStale = true;
  static char lastClock[6] = "";

  if (evcc::latest(state)) dirty = true;

  switch (evcc::takeCommandResult()) {
    case CommandResult::Failed:
      pendingMode = ChargeMode::None;
      flash("Modus konnte nicht gesetzt werden");
      break;
    default:
      break;
  }
  if (pendingMode != ChargeMode::None) {
    if (state.valid && state.updatedMs > pendingSince && state.mode == pendingMode) {
      pendingMode = ChargeMode::None;
      dirty = true;
    } else if (millis() - pendingSince > MODE_CONFIRM_TIMEOUT_MS) {
      pendingMode = ChargeMode::None;
      flash("evcc hat den Modus nicht übernommen");
    }
  }
  if (flashMsg && (int32_t)(millis() - flashUntil) > 0) {
    flashMsg = nullptr;
    dirty = true;
  }

  // Bildschirm wecken, wenn ein Auto angesteckt wird oder das Laden beginnt/endet
  if (state.connected != lastConnected || state.charging != lastCharging) {
    lastConnected = state.connected;
    lastCharging = state.charging;
    lastActivity = millis();
  }

  const bool wifi = WiFi.status() == WL_CONNECTED;
  const bool stale = !state.valid || millis() - state.updatedMs > STALE_AFTER_MS;
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
    UiView v;
    v.state = preview ? previewState : state;
    v.wifiConnected = preview || wifi;
    v.stale = preview ? previewStale : stale;
    v.pendingMode = pendingMode;
    v.flash = flashMsg;
    strlcpy(v.clock, clock, sizeof(v.clock));
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
  if (display::lastTouchMs() > lastActivity) lastActivity = display::lastTouchMs();

  const bool keepOn = portalActive || (state.valid && state.charging) ||
                      settings::get().screenTimeoutMin == 0 ||
                      millis() - lastActivity < settings::get().screenTimeoutMin * 60000UL;
  display::setSleeping(!keepOn);

  const int target = keepOn ? BRIGHTNESS : 0;
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
  if (!preview::scene(web.arg("s").c_str(), p, previewStale)) {
    previewUntil = 0;
    dirty = true;
    web.send(200, "text/plain",
             String("Vorschau beendet. Zustaende: ?s=") + preview::NAMES + " [&dark=0|1]\n");
    return;
  }
  p.updatedMs = millis();
  if (state.valid) strlcpy(p.title, state.title, sizeof(p.title));

  previewState = p;
  previewDark = web.hasArg("dark") ? web.arg("dark").toInt() != 0 : -1;
  previewUntil = millis() + PREVIEW_DURATION_MS;
  lastActivity = millis();
  dirty = true;
  web.send(200, "text/plain", "Vorschau aktiv fuer 60 s\n");
}

static void startWebServer() {
  web.on("/screenshot.bmp", []() { display::sendScreenshot(web); });
  web.on("/preview", handlePreview);
  // Lasttest gegen Bildstörungen: löst eine WLAN-Suche aus (wie das Einstellungsportal)
  web.on("/wifiscan", []() {
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
#ifdef OTA_PASSWORD
    ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
    ArduinoOTA.onStart([]() {
      display::setBrightness(BRIGHTNESS);
      ui::showMessage(ICON_SETTINGS, "Update", "Neue Firmware wird installiert …");
      lv_refr_now(nullptr);
    });
    ArduinoOTA.begin();
    otaStarted = true;
  }
}

// ---------- Arduino ----------

void setup() {
  Serial.begin(115200);

  settings::load();
  display::begin(settings::get().rotation);
  brightness = BRIGHTNESS;
  lastActivity = millis();

  ui::begin(onModeSelected, openSettings);
  ui::onMessageTap(closeSettings);
  ui::showMessage(ICON_SETTINGS, "Verbinde …", "WLAN-Verbindung wird aufgebaut");
  lv_refr_now(nullptr);
  display::setBrightness(BRIGHTNESS);  // erst einschalten, wenn ein Bild im Speicher steht

  WiFi.mode(WIFI_STA);
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

  evcc::begin();
}

void loop() {
  lv_timer_handler();
  wm.process();

  if (WiFi.status() == WL_CONNECTED) {
    startNetworkServices();
    ArduinoOTA.handle();
    web.handleClient();
  }

  if (restartAt && (int32_t)(millis() - restartAt) > 0) ESP.restart();

  if (portalActive && !wm.getConfigPortalActive()) {
    portalActive = false;
    // Portal ohne WLAN abgelaufen (z. B. Router nach Stromausfall noch nicht bereit) → neu versuchen
    if (WiFi.status() != WL_CONNECTED) ESP.restart();
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
