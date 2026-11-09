#include "evcc_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"
#include "settings.h"

static SemaphoreHandle_t lock;
static uint8_t lpCount = 1;
static uint8_t lpNumber[MAX_LOADPOINTS];  // Nummer in evcc, 1-basiert
static EvccState shared[MAX_LOADPOINTS];
static uint32_t sharedVersion = 0;
static uint32_t readVersion = 0;
// Je Ladepunkt nur der zuletzt gewählte Modus; None = nichts zu tun
static ChargeMode wanted[MAX_LOADPOINTS];
static volatile CommandResult commandResult[MAX_LOADPOINTS];
static int8_t wantedBoost[MAX_LOADPOINTS];  // -1 = nichts zu tun, sonst 0/1 (in begin() gesetzt)
static volatile CommandResult boostResult[MAX_LOADPOINTS];

static String baseUrl[MAX_LOADPOINTS];
static String stateUrl;

static String urlEncode(const char* s) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (; *s; s++) {
    const char c = *s;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

static int httpRequest(const String& url, bool post, String* body) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  if (!http.begin(client, url)) return -1;
  const int code = post ? http.POST((uint8_t*)nullptr, 0) : http.GET();
  if (body && code == HTTP_CODE_OK) *body = http.getString();
  http.end();
  return code;
}

static ChargeMode deriveMode(const char* mode, const char* alwaysCharge, bool& off) {
  off = false;
  if (!strcmp(mode, "now")) return ChargeMode::Fast;
  if (!strcmp(mode, "minpv")) return ChargeMode::Slow;  // evcc vor 0.316
  if (!strcmp(mode, "pv")) return ChargeMode::Smart;    // evcc vor 0.316
  if (!strcmp(mode, "smart")) {
    return (!strcmp(alwaysCharge, "on") || !strcmp(alwaysCharge, "once")) ? ChargeMode::Slow
                                                                           : ChargeMode::Smart;
  }
  off = !strcmp(mode, "off");
  return ChargeMode::None;
}

static void parseLoadpoint(JsonObject root, JsonObject lp, EvccState& s) {
  s.valid = true;
  s.updatedMs = millis();
  strlcpy(s.title, lp["title"] | "Ladepunkt", sizeof(s.title));
  s.mode = deriveMode(lp["mode"] | "", lp["alwaysCharge"] | "", s.modeOff);
  s.chargePower = lp["chargePower"] | 0.0f;
  s.charging = lp["charging"] | false;
  s.connected = lp["connected"] | false;
  s.heating = lp["chargerFeatureHeating"] | false;
  s.chargedEnergy = lp["chargedEnergy"] | 0.0f;
  s.chargeDuration = (uint32_t)(lp["chargeDuration"] | 0.0f);
  s.phasesActive = lp["phasesActive"] | 0;
  s.sessionSolarPct = lp["sessionSolarPercentage"] | 0.0f;
  s.greenShare = root["green"] | -1.0f;
  s.vehicleSoc = lp["vehicleSoc"] | 0.0f;
  s.limitSoc = lp["limitSoc"] | 0.0f;
  s.vehicleLimitSoc = lp["vehicleLimitSoc"] | 0.0f;
  strlcpy(s.vehicleTitle, lp["vehicleTitle"] | "", sizeof(s.vehicleTitle));
  strlcpy(s.pvAction, lp["pvAction"] | "", sizeof(s.pvAction));
  s.pvRemaining = (uint32_t)(lp["pvRemaining"] | 0.0f);
  s.planActive = lp["planActive"] | false;
  s.smartCostActive = lp["smartCostActive"] | false;
  s.hasBoost = lp["batteryBoost"].is<bool>();
  s.batteryBoost = lp["batteryBoost"] | false;
  if (root["pv"].is<float>()) s.pvPower = root["pv"];
  s.homePower = root["home"] | 0.0f;
  if (root["bat"].is<float>()) s.batterySoc = root["bat"];
  s.batteryPower = root["batp"] | 0.0f;
  s.prioritySoc = root["prio"] | 0.0f;
  s.hasGrid = root["grid"].is<float>();
  s.gridPower = root["grid"] | 0.0f;
  if (root["fc"][0].is<float>()) s.solarToday = root["fc"][0];
  if (root["fc"][1].is<float>()) s.solarTomorrow = root["fc"][1];
  // Phasen, mit denen Smart startet: fest eingestellt oder bei Umschaltung (0 = automatisch) einphasig
  const int phasesConfigured = lp["phasesConfigured"] | 0;
  const int phases = phasesConfigured == 1 || phasesConfigured == 3 ? phasesConfigured
                     : (lp["chargerPhases1p3p"] | false)              ? 1
                                                                       : 3;
  s.minChargePower = (lp["minCurrent"] | 0.0f) * 230.0f * phases;
}

static void poll() {
  String body;
  if (httpRequest(stateUrl, false, &body) != HTTP_CODE_OK) return;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  // Ältere evcc-Versionen verpacken die Antwort in {"result": ...}
  JsonObject root = doc["result"].is<JsonObject>() ? doc["result"].as<JsonObject>()
                                                   : doc.as<JsonObject>();
  JsonArray lps = root["lps"];
  if (lps.isNull()) return;

  EvccState all[MAX_LOADPOINTS];
  for (uint8_t i = 0; i < lpCount; i++) {
    JsonObject lp = lps[i];
    if (!lp.isNull()) {
      parseLoadpoint(root, lp, all[i]);
    } else {
      all[i].missing = true;  // falsche Nummer in den Einstellungen
      snprintf(all[i].title, sizeof(all[i].title), "Ladepunkt %u", lpNumber[i]);
      all[i].updatedMs = millis();
    }
  }

  xSemaphoreTake(lock, portMAX_DELAY);
  for (uint8_t i = 0; i < lpCount; i++) shared[i] = all[i];
  sharedVersion++;
  xSemaphoreGive(lock);
}

static bool post(uint8_t index, const char* path) {
  const int code = httpRequest(baseUrl[index] + path, true, nullptr);
  log_i("POST %s%s -> %d", baseUrl[index].c_str(), path, code);
  return code == HTTP_CODE_OK;
}

static bool setMode(uint8_t index, ChargeMode mode) {
  switch (mode) {
    case ChargeMode::Fast:
      return post(index, "/mode/now");
    case ChargeMode::Smart:
    case ChargeMode::Slow: {
      const bool always = mode == ChargeMode::Slow;
      // evcc ≥ 0.316: "immer laden" + smart
      if (post(index, always ? "/alwayscharge/on" : "/alwayscharge/off")) return post(index, "/mode/smart");
      // Ältere Versionen kennen alwayscharge nicht, dafür pv/minpv
      return post(index, always ? "/mode/minpv" : "/mode/pv");
    }
    default:
      return false;
  }
}

static bool setBoost(uint8_t index, bool on) { return post(index, on ? "/batteryboost/1" : "/batteryboost/0"); }

static void task(void*) {
  uint32_t lastPoll = 0;
  for (;;) {
    for (uint8_t i = 0; i < lpCount; i++) {
      xSemaphoreTake(lock, portMAX_DELAY);
      const ChargeMode cmd = wanted[i];
      wanted[i] = ChargeMode::None;
      xSemaphoreGive(lock);
      if (cmd == ChargeMode::None) continue;
      if (WiFi.status() == WL_CONNECTED) {
        commandResult[i] = setMode(i, cmd) ? CommandResult::Ok : CommandResult::Failed;
        lastPoll = 0;  // sofort neuen Zustand holen
      } else {
        commandResult[i] = CommandResult::Failed;
      }
    }
    for (uint8_t i = 0; i < lpCount; i++) {
      xSemaphoreTake(lock, portMAX_DELAY);
      const int8_t boost = wantedBoost[i];
      wantedBoost[i] = -1;
      xSemaphoreGive(lock);
      if (boost < 0) continue;
      const bool ok = WiFi.status() == WL_CONNECTED && setBoost(i, boost);
      boostResult[i] = ok ? CommandResult::Ok : CommandResult::Failed;
      if (ok) lastPoll = 0;
    }
    if (WiFi.status() == WL_CONNECTED && (lastPoll == 0 || millis() - lastPoll >= POLL_INTERVAL_MS)) {
      lastPoll = millis() | 1;
      poll();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void evcc::begin() {
  const Settings& s = settings::get();
  const String server = "http://" + s.evccHost + ":" + String(s.evccPort);
  lpCount = s.loadpointCount;
  for (int8_t& b : wantedBoost) b = -1;

  // Ein Abruf für alle Ladepunkte; fehlt einer in evcc, steht an seiner Stelle null
  String lps;
  for (uint8_t i = 0; i < lpCount; i++) {
    lpNumber[i] = s.loadpoints[i];
    baseUrl[i] = server + "/api/loadpoints/" + String(s.loadpoints[i]);
    if (i) lps += ',';
    lps += s.loadpoints[i] - 1;
  }
  // Ältere evcc-Versionen liefern Akku und Netz als batterySoc, batteryPower und gridPower,
  // neuere unter battery.soc, battery.power und grid.power
  char jq[1024];
  snprintf(jq, sizeof(jq),
           "{lps:[.loadpoints[%s]|if . then {title,mode,alwaysCharge,chargePower,charging,connected,chargedEnergy,"
           "chargeDuration,phasesActive,sessionSolarPercentage,vehicleTitle,vehicleSoc,"
           "limitSoc:(.effectiveLimitSoc // .limitSoc),vehicleLimitSoc,pvAction,pvRemaining,"
           "planActive,smartCostActive,batteryBoost,minCurrent:(.effectiveMinCurrent // .minCurrent),phasesConfigured,"
           "chargerPhases1p3p,chargerFeatureHeating} else null end],green:.greenShareLoadpoints,pv:.pvPower,home:.homePower,"
           "bat:((try (if (.battery.devices|length)>0 then .battery.soc else null end) catch null) // .batterySoc),"
           "batp:((try (if (.battery.devices|length)>0 then .battery.power else null end) catch null) // .batteryPower),"
           "grid:((try .grid.power catch null) // .gridPower),prio:.prioritySoc,"
           "fc:(try [.forecast.solar.today.energy,.forecast.solar.tomorrow.energy] catch null)}",
           lps.c_str());
  stateUrl = server + "/api/state?jq=" + urlEncode(jq);

  lock = xSemaphoreCreateMutex();
  // Netzwerk auf Kern 0, damit LVGL (loop, Kern 1) flüssig bleibt
  xTaskCreatePinnedToCore(task, "evcc", 8192, nullptr, 1, nullptr, 0);
}

uint8_t evcc::count() { return lpCount; }

bool evcc::latest(EvccState* out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  const bool changed = sharedVersion != readVersion;
  for (uint8_t i = 0; i < lpCount; i++) out[i] = shared[i];
  readVersion = sharedVersion;
  xSemaphoreGive(lock);
  return changed;
}

void evcc::requestMode(uint8_t index, ChargeMode mode) {
  if (index >= lpCount) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  wanted[index] = mode;  // nur der zuletzt gewählte Modus zählt
  xSemaphoreGive(lock);
}

CommandResult evcc::takeCommandResult(uint8_t index) {
  const CommandResult r = commandResult[index];
  commandResult[index] = CommandResult::None;
  return r;
}

void evcc::requestBoost(uint8_t index, bool on) {
  if (index >= lpCount) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  wantedBoost[index] = on;
  xSemaphoreGive(lock);
}

CommandResult evcc::takeBoostResult(uint8_t index) {
  const CommandResult r = boostResult[index];
  boostResult[index] = CommandResult::None;
  return r;
}
