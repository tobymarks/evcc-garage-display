#include "evcc_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"
#include "settings.h"

static SemaphoreHandle_t lock;
static QueueHandle_t commands;
static EvccState shared;
static uint32_t sharedVersion = 0;
static uint32_t readVersion = 0;
static volatile CommandResult commandResult = CommandResult::None;

static String baseUrl;
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

static void poll() {
  String body;
  if (httpRequest(stateUrl, false, &body) != HTTP_CODE_OK) return;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  // Ältere evcc-Versionen verpacken die Antwort in {"result": ...}
  JsonObject root = doc["result"].is<JsonObject>() ? doc["result"].as<JsonObject>()
                                                   : doc.as<JsonObject>();
  JsonObject lp = root["lp"];
  if (lp.isNull()) return;

  EvccState s;
  s.valid = true;
  s.updatedMs = millis();
  strlcpy(s.title, lp["title"] | "Ladepunkt", sizeof(s.title));
  s.mode = deriveMode(lp["mode"] | "", lp["alwaysCharge"] | "", s.modeOff);
  s.chargePower = lp["chargePower"] | 0.0f;
  s.charging = lp["charging"] | false;
  s.connected = lp["connected"] | false;
  s.chargedEnergy = lp["chargedEnergy"] | 0.0f;
  s.chargeDuration = (uint32_t)(lp["chargeDuration"] | 0.0f);
  s.phasesActive = lp["phasesActive"] | 0;
  s.sessionSolarPct = lp["sessionSolarPercentage"] | 0.0f;
  s.greenShare = root["green"] | -1.0f;
  s.vehicleSoc = lp["vehicleSoc"] | 0.0f;
  strlcpy(s.vehicleTitle, lp["vehicleTitle"] | "", sizeof(s.vehicleTitle));
  strlcpy(s.pvAction, lp["pvAction"] | "", sizeof(s.pvAction));
  s.pvRemaining = (uint32_t)(lp["pvRemaining"] | 0.0f);
  s.planActive = lp["planActive"] | false;
  s.smartCostActive = lp["smartCostActive"] | false;

  xSemaphoreTake(lock, portMAX_DELAY);
  shared = s;
  sharedVersion++;
  xSemaphoreGive(lock);
}

static bool post(const String& path) {
  const int code = httpRequest(baseUrl + path, true, nullptr);
  log_i("POST %s -> %d", path.c_str(), code);
  return code == HTTP_CODE_OK;
}

static bool setMode(ChargeMode mode) {
  switch (mode) {
    case ChargeMode::Fast:
      return post("/mode/now");
    case ChargeMode::Smart:
    case ChargeMode::Slow: {
      const bool always = mode == ChargeMode::Slow;
      // evcc ≥ 0.316: "immer laden" + smart
      if (post(always ? "/alwayscharge/on" : "/alwayscharge/off")) return post("/mode/smart");
      // Ältere Versionen kennen alwayscharge nicht, dafür pv/minpv
      return post(always ? "/mode/minpv" : "/mode/pv");
    }
    default:
      return false;
  }
}

static void task(void*) {
  uint32_t lastPoll = 0;
  for (;;) {
    ChargeMode cmd;
    if (xQueueReceive(commands, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (WiFi.status() == WL_CONNECTED) {
        commandResult = setMode(cmd) ? CommandResult::Ok : CommandResult::Failed;
        lastPoll = 0;  // sofort neuen Zustand holen
      } else {
        commandResult = CommandResult::Failed;
      }
    }
    if (WiFi.status() == WL_CONNECTED && (lastPoll == 0 || millis() - lastPoll >= POLL_INTERVAL_MS)) {
      lastPoll = millis() | 1;
      poll();
    }
  }
}

void evcc::begin() {
  const Settings& s = settings::get();
  baseUrl = "http://" + s.evccHost + ":" + String(s.evccPort) + "/api/loadpoints/" + String(s.loadpoint);

  char jq[400];
  snprintf(jq, sizeof(jq),
           "{lp:.loadpoints[%d]|{title,mode,alwaysCharge,chargePower,charging,connected,chargedEnergy,"
           "chargeDuration,phasesActive,sessionSolarPercentage,vehicleTitle,vehicleSoc,pvAction,pvRemaining,"
           "planActive,smartCostActive},green:.greenShareLoadpoints}",
           s.loadpoint - 1);
  stateUrl = "http://" + s.evccHost + ":" + String(s.evccPort) + "/api/state?jq=" + urlEncode(jq);

  lock = xSemaphoreCreateMutex();
  commands = xQueueCreate(2, sizeof(ChargeMode));
  // Netzwerk auf Kern 0, damit LVGL (loop, Kern 1) flüssig bleibt
  xTaskCreatePinnedToCore(task, "evcc", 8192, nullptr, 1, nullptr, 0);
}

bool evcc::latest(EvccState& out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  const bool changed = sharedVersion != readVersion;
  out = shared;
  readVersion = sharedVersion;
  xSemaphoreGive(lock);
  return changed;
}

void evcc::requestMode(ChargeMode mode) {
  xQueueReset(commands);  // nur der zuletzt gewählte Modus zählt
  xQueueSend(commands, &mode, 0);
}

CommandResult evcc::takeCommandResult() {
  const CommandResult r = commandResult;
  commandResult = CommandResult::None;
  return r;
}
