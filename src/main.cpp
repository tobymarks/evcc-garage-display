// evcc-Garagendisplay: zeigt die Ladeleistung eines evcc-Ladepunkts und schaltet
// zwischen PV, Smart und Schnell um. Hardware: Guition ESP32-4848S040C (4", 480x480).

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
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
static ChargeMode pendingMode[MAX_LOADPOINTS];
static uint32_t pendingSince[MAX_LOADPOINTS];
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

// Übernimmt die Werte aus dem Portal oder von /settings (gleiche Feldnamen) und startet neu
static void applySettings(std::function<String(const char*)> value) {
  Settings& s = settings::get();
  s.evccHost = value("host");
  s.evccHost.trim();
  s.evccPort = constrain(value("port").toInt(), 1, 65535);
  settings::parseLoadpoints(value("lp").c_str(), s);
  s.latitude = value("lat").toFloat();
  s.longitude = value("lon").toFloat();
  s.rotation = constrain(value("rot").toInt(), 0, 3);
  s.theme = constrain(value("theme").toInt(), 0, 2);
  s.screenTimeoutMin = constrain(value("timeout").toInt(), 0, 240);
  s.autoUpdate = value("autoupd").toInt() != 0;
  settings::save();
  restartAt = millis() + 2000;  // Antwortseite noch ausliefern, dann neu starten
}

static void saveParams() {
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

static void onModeSelected(ChargeMode mode) {
  lastActivity = millis();
  const ChargeMode shown = pendingMode[current] != ChargeMode::None ? pendingMode[current] : states[current].mode;
  if (mode == shown) return;
  pendingMode[current] = mode;
  pendingSince[current] = millis();
  flashMsg = nullptr;
  evcc::requestMode(current, mode);
  dirty = true;
}

static void showLoadpoint(uint8_t index) {
  if (index == current || index >= evcc::count()) return;
  current = index;
  flashMsg = nullptr;  // Fehlermeldungen gehören zum vorher gezeigten Ladepunkt
  dirty = true;
}

// Wischen nach links → nächster Ladepunkt
static void onSwipe(int delta) {
  lastActivity = millis();
  showLoadpoint(current + delta);
}

static bool anyCharging() {
  for (uint8_t i = 0; i < evcc::count(); i++)
    if (states[i].valid && states[i].charging) return true;
  return false;
}

static bool anyPending() {
  for (uint8_t i = 0; i < evcc::count(); i++)
    if (pendingMode[i] != ChargeMode::None) return true;
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

static void updateState() {
  static bool lastConnected[MAX_LOADPOINTS], lastCharging[MAX_LOADPOINTS];
  static bool lastWifi = false, lastStale = true;
  static char lastClock[6] = "";

  if (evcc::latest(states)) dirty = true;

  const uint32_t touched = display::lastTouchMs();
  const bool inUse = touched && millis() - touched < AUTO_SWITCH_AFTER_TOUCH_MS;
  int jumpTo = -1;  // Ladepunkt, an dem sich gerade etwas getan hat
  for (uint8_t i = 0; i < evcc::count(); i++) {
    const EvccState& st = states[i];
    // Fehler nur melden, wenn der Ladepunkt gerade zu sehen ist
    const char* error = nullptr;
    if (evcc::takeCommandResult(i) == CommandResult::Failed) {
      pendingMode[i] = ChargeMode::None;
      error = "Modus konnte nicht gesetzt werden";
    }
    if (pendingMode[i] != ChargeMode::None) {
      if (st.valid && st.updatedMs > pendingSince[i] && st.mode == pendingMode[i]) {
        pendingMode[i] = ChargeMode::None;
        dirty = true;
      } else if (millis() - pendingSince[i] > MODE_CONFIRM_TIMEOUT_MS) {
        pendingMode[i] = ChargeMode::None;
        error = "evcc hat den Modus nicht übernommen";
      }
    }
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
  // Nicht wegspringen, solange jemand das Display bedient
  if (jumpTo >= 0 && !inUse) showLoadpoint(jumpTo);
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
    UiView v;
    v.state = preview ? previewState : state;
    v.wifiConnected = preview || wifi;
    v.stale = preview ? previewStale : stale;
    v.pendingMode = pendingMode[current];
    v.page = current;
    v.pageCount = evcc::count();
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

  const bool keepOn = portalActive || anyCharging() ||
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
  if (states[current].valid) strlcpy(p.title, states[current].title, sizeof(p.title));

  previewState = p;
  previewDark = web.hasArg("dark") ? web.arg("dark").toInt() != 0 : -1;
  previewUntil = millis() + PREVIEW_DURATION_MS;
  lastActivity = millis();
  dirty = true;
  web.send(200, "text/plain", "Vorschau aktiv fuer 60 s\n");
}

static void handleDiag() {
  static const char* const REASONS[] = {"unbekannt", "Einschalten", "extern", "Software", "Absturz",
                                        "Interrupt-Watchdog", "Task-Watchdog", "Watchdog", "Deep Sleep",
                                        "Unterspannung", "SDIO"};
  const esp_reset_reason_t reason = esp_reset_reason();
  char buf[160];
  String s;
  snprintf(buf, sizeof(buf), "Firmware %s\nLaufzeit %lu s\nNeustartgrund: %s\n", FW_VERSION, millis() / 1000,
           reason < sizeof(REASONS) / sizeof(*REASONS) ? REASONS[reason] : "anderer");
  s += buf;
  snprintf(buf, sizeof(buf), "Interner RAM frei %u, minimal %u, größter Block %u\nPSRAM frei %u\n",
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  s += buf;
  // Startete eine neue Firmware nicht sauber, hat der Bootloader die vorherige wiederhergestellt
  esp_app_desc_t desc;
  const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
  if (invalid && esp_ota_get_partition_description(invalid, &desc) == ESP_OK) {
    snprintf(buf, sizeof(buf), "Zurückgerollt: Firmware in %s (gebaut %s %s) startete nicht sauber\n", invalid->label,
             desc.date, desc.time);
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
padding:.8rem;border:0;border-radius:.5rem;background:var(--a);color:#000}footer{margin-top:2rem;opacity:.5;font-size:.85rem}
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
    applySettings([](const char* key) { return web.arg(key); });
    web.sendHeader("Refresh", "15; url=/settings");
    web.send(200, "text/html; charset=utf-8",
             String(FPSTR(SETTINGS_HEAD)) +
                 "<p>Gespeichert. Das Display startet neu, die Seite lädt in einigen Sekunden wieder.</p></body></html>");
    return;
  }

  const Settings& s = settings::get();
  String h = FPSTR(SETTINGS_HEAD);
  h.reserve(4096);
  h += "<form method=\"post\">";
  h += "<div class=\"row\"><div><label>evcc-Adresse</label><input name=\"host\" required value=\"" +
       htmlEscape(s.evccHost) + "\"></div>";
  h += "<div style=\"flex:.45\"><label>Port</label><input name=\"port\" type=\"number\" min=\"1\" max=\"65535\" value=\"" +
       String(s.evccPort) + "\"></div></div>";
  h += "<label>Ladepunkte<small>Nummer wie in evcc (1 = erster). Mehrere mit Komma, z. B. 1,2,3 – dann wischen "
       "zum Wechseln.</small></label><input name=\"lp\" inputmode=\"numeric\" pattern=\"[0-9 ,]*\" value=\"" +
       settings::loadpointsText(s) + "\">";
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
  h += "<label>Drehung</label><select name=\"rot\">" + option(0, s.rotation, "0°") +
       option(1, s.rotation, "90° im Uhrzeigersinn") + option(2, s.rotation, "180°") +
       option(3, s.rotation, "270° im Uhrzeigersinn") + "</select>";
  h += "<label>Automatische Updates</label><select name=\"autoupd\">" + option(1, s.autoUpdate, "An") +
       option(0, s.autoUpdate, "Aus") + "</select>";
  h += "<button>Speichern und neu starten</button></form><footer>Firmware " FW_VERSION
       " · WLAN ändern: Titel am Display 3 s gedrückt halten</footer></body></html>";
  web.send(200, "text/html; charset=utf-8", h);
}

static void startWebServer() {
  web.on("/", []() {
    web.sendHeader("Location", "/settings");
    web.send(302);
  });
  web.on("/settings", handleSettings);
  web.on("/screenshot.bmp", []() { display::sendScreenshot(web); });
  web.on("/preview", handlePreview);
  // Update-Status; ?check=1 prüft sofort, ?install=1 installiert eine gefundene neue Version sofort
  web.on("/update", []() {
    if (web.hasArg("check")) updater::checkNow();
    if (web.hasArg("install")) updater::installNow();
    web.send(200, "text/plain; charset=utf-8", updater::status());
  });
  // Laufzeit, Neustartgrund, Speicher und der zuletzt gespeicherte Absturz (Coredump im Flash)
  web.on("/diag", handleDiag);
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

  ui::begin(onModeSelected, openSettings, onSwipe);
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
  updater::begin();
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

  if (WiFi.status() == WL_CONNECTED) {
    startNetworkServices();
    ArduinoOTA.handle();
    web.handleClient();
  }
  handleUpdates();

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
