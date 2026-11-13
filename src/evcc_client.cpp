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
static EvccVehicles sharedVehicles;
static EvccSolar sharedSolar;
// Je Art und Ladepunkt nur der zuletzt angefragte Befehl
struct Slot {
  uint32_t wantedId = 0;  // 0 = nichts zu tun
  ChargeMode mode = ChargeMode::None;
  bool on = false;
  char vehicle[VEHICLE_NAME_LEN] = "";
  bool limitEnergy = false;  // Limit in kWh statt in Prozent
  float limit = 0;
  CommandStatus done;
};
static Slot slots[(int)Command::Count][MAX_LOADPOINTS];
static uint32_t nextId = 0;
static TaskHandle_t taskHandle;

static String baseUrl[MAX_LOADPOINTS];
static String stateUrl;
static String serverUrl;

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

static int httpRequest(const String& url, const char* method, String* body) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  if (!http.begin(client, url)) return -1;
  const int code = !strcmp(method, "GET")    ? http.GET()
                   : !strcmp(method, "POST") ? http.POST((uint8_t*)nullptr, 0)
                                             : http.sendRequest(method);
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
  s.limitEnergy = lp["limitEnergy"] | 0.0f;
  strlcpy(s.vehicleName, lp["vehicleName"] | "", sizeof(s.vehicleName));
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

static void parseVehicles(JsonObject root, EvccVehicles& out) {
  out.count = 0;
  for (JsonObject v : root["veh"].as<JsonArray>()) {
    if (out.count >= MAX_VEHICLES) break;
    const char* name = v["n"] | "";
    // Ein abgeschnittener Name ließe sich nicht setzen – solche Fahrzeuge fehlen in der Auswahl
    if (!name[0] || strlen(name) >= VEHICLE_NAME_LEN) continue;
    strlcpy(out.names[out.count], name, sizeof(out.names[0]));
    strlcpy(out.titles[out.count], v["t"] | name, sizeof(out.titles[0]));
    out.offline[out.count] = v["o"] | false;
    out.count++;
  }
}

// Leistung je PV-Anlage aus dem normalen Zustand. Mehr Anlagen, als Zeilen auf die Seite passen:
// die ersten einzeln, der Rest zusammen als "Weitere".
static void parsePlants(JsonObject root, EvccSolar& out) {
  out.plantCount = 0;
  JsonArray pvs = root["pvs"];
  const size_t total = pvs.size();
  out.plantTotal = total;
  size_t i = 0;
  for (JsonObject pv : pvs) {
    const float power = pv["p"] | 0.0f;
    if (total > MAX_PV_PLANTS && i >= MAX_PV_PLANTS - 1) {
      if (i == MAX_PV_PLANTS - 1) {
        strlcpy(out.plantTitle[MAX_PV_PLANTS - 1], "Weitere", sizeof(out.plantTitle[0]));
        out.plantPower[MAX_PV_PLANTS - 1] = 0;
        out.plantCount = MAX_PV_PLANTS;
      }
      out.plantPower[MAX_PV_PLANTS - 1] += power;
    } else {
      strlcpy(out.plantTitle[i], pv["t"] | "Anlage", sizeof(out.plantTitle[0]));
      out.plantPower[i] = power;
      out.plantCount = i + 1;
    }
    i++;
  }
}

// "2026-11-13T00:00:00+01:00" in Ortszeit – so erwartet evcc Beginn und Ende
static void isoLocal(time_t t, char* buf, size_t len) {
  struct tm tm;
  localtime_r(&t, &tm);
  char zone[8];
  strftime(zone, sizeof(zone), "%z", &tm);  // "+0100"
  const size_t n = strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm);
  snprintf(buf + n, len - n, "%.3s:%.2s", zone, zone + 3);
}

// Stundenwerte von heute für die Solarseite: Erzeugung und Prognose der vergangenen Stunden aus dem
// Verlauf von evcc, die Prognose der kommenden Stunden aus der Solarprognose. false, wenn evcc den
// Verlauf nicht kennt (ältere Version) oder die Uhrzeit noch fehlt.
static bool fetchSolar() {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  struct tm tm;
  localtime_r(&now, &tm);
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  tm.tm_isdst = -1;
  const time_t dayStart = mktime(&tm);
  tm.tm_mday += 1;
  tm.tm_isdst = -1;
  const time_t dayEnd = mktime(&tm);
  char from[32], to[32];
  isoLocal(dayStart, from, sizeof(from));
  isoLocal(dayEnd, to, sizeof(to));

  static EvccSolar solar;  // static: der Stack des Tasks bleibt für HTTP und JSON frei
  solar = EvccSolar();
  {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(3000);
    http.useHTTP10(true);  // ohne Chunked-Kodierung, damit sich die Antwort direkt aus dem Stream lesen lässt
    if (!http.begin(client, serverUrl + "/api/history/energy?from=" + urlEncode(from) + "&to=" + urlEncode(to) +
                                "&aggregate=hour"))
      return false;
    if (http.GET() != HTTP_CODE_OK) {
      http.end();
      return false;
    }
    // Die Antwort enthält alle Zähler (rund 20 KB); gebraucht werden nur Art, Beginn und Energie
    JsonDocument filter;
    filter[0]["group"] = true;
    filter[0]["data"][0]["start"] = true;
    filter[0]["data"][0]["energy"] = true;
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) return false;
    for (JsonObject series : doc.as<JsonArray>()) {  // "null", solange es für heute noch keine Werte gibt
      const char* group = series["group"] | "";
      float* target = !strcmp(group, "pv") ? solar.hourEnergy : !strcmp(group, "forecast") ? solar.hourForecast : nullptr;
      if (!target) continue;
      for (JsonObject slot : series["data"].as<JsonArray>()) {
        const char* start = slot["start"] | "";
        if (strlen(start) < 13) continue;
        const int hour = (start[11] - '0') * 10 + (start[12] - '0');
        if (hour >= 0 && hour < 24) target[hour] += slot["energy"] | 0.0f;
      }
    }
  }

  // Kommende Stunden: Viertelstundenwerte der Prognose (W), von evcc je Stunde zu Wh zusammengefasst
  char jq[224];
  snprintf(jq, sizeof(jq),
           "try ([.forecast.solar.timeseries[]|select(.[0]>=%lld and .[0]<%lld)]|group_by((.[0]-%lld)/3600|floor)|"
           "map([((.[0][0]-%lld)/3600|floor),((map(.[1])|add)/4)])) catch []",
           (long long)dayStart, (long long)dayEnd, (long long)dayStart, (long long)dayStart);
  String body;
  if (httpRequest(serverUrl + "/api/state?jq=" + urlEncode(jq), "GET", &body) == HTTP_CODE_OK) {
    JsonDocument doc;
    if (!deserializeJson(doc, body)) {
      JsonArray rows = doc["result"].is<JsonArray>() ? doc["result"].as<JsonArray>() : doc.as<JsonArray>();
      for (JsonArray row : rows) {
        const int hour = row[0] | -1;
        const float kwh = (row[1] | 0.0f) / 1000.0f;
        // Für die laufende Stunde kennt jede Quelle nur einen Teil: der größere Wert zählt
        if (hour >= 0 && hour < 24 && kwh > solar.hourForecast[hour]) solar.hourForecast[hour] = kwh;
      }
    }
  }

  for (int h = 0; h < 24; h++) {
    solar.today += solar.hourEnergy[h];
    solar.forecastToday += solar.hourForecast[h];
  }
  solar.valid = true;

  xSemaphoreTake(lock, portMAX_DELAY);
  // Die Leistung je Anlage kommt mit jeder normalen Abfrage und bleibt stehen
  solar.plantTotal = sharedSolar.plantTotal;
  solar.plantCount = sharedSolar.plantCount;
  memcpy(solar.plantTitle, sharedSolar.plantTitle, sizeof(solar.plantTitle));
  memcpy(solar.plantPower, sharedSolar.plantPower, sizeof(solar.plantPower));
  sharedSolar = solar;
  sharedVersion++;
  xSemaphoreGive(lock);
  return true;
}

static void poll() {
  const uint32_t started = millis();
  String body;
  if (httpRequest(stateUrl, "GET", &body) != HTTP_CODE_OK) return;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  // Ältere evcc-Versionen verpacken die Antwort in {"result": ...}
  JsonObject root = doc["result"].is<JsonObject>() ? doc["result"].as<JsonObject>()
                                                   : doc.as<JsonObject>();
  JsonArray lps = root["lps"];
  if (lps.isNull()) return;

  // static: nur dieser Task ruft poll() auf, und der Stack bleibt für HTTP und JSON frei
  static EvccState all[MAX_LOADPOINTS];
  static EvccVehicles vehicles;
  static EvccSolar plants;
  parsePlants(root, plants);
  for (uint8_t i = 0; i < lpCount; i++) {
    all[i] = EvccState();
    JsonObject lp = lps[i];
    if (!lp.isNull()) {
      parseLoadpoint(root, lp, all[i]);
    } else {
      all[i].missing = true;  // falsche Nummer in den Einstellungen
      snprintf(all[i].title, sizeof(all[i].title), "Ladepunkt %u", lpNumber[i]);
      all[i].updatedMs = millis();
    }
    all[i].polledMs = started;
  }
  parseVehicles(root, vehicles);
  // Wie in evcc: Prozent-Limit, sobald ein Ladestand da ist oder das Fahrzeug einen liefern kann
  for (uint8_t i = 0; i < lpCount; i++) {
    EvccState& st = all[i];
    st.socBased = st.vehicleSoc > 0;
    for (uint8_t k = 0; k < vehicles.count && !st.socBased && st.vehicleName[0]; k++)
      if (!strcmp(vehicles.names[k], st.vehicleName)) st.socBased = !vehicles.offline[k];
  }

  xSemaphoreTake(lock, portMAX_DELAY);
  for (uint8_t i = 0; i < lpCount; i++) shared[i] = all[i];
  sharedVehicles = vehicles;
  sharedSolar.plantTotal = plants.plantTotal;
  sharedSolar.plantCount = plants.plantCount;
  memcpy(sharedSolar.plantTitle, plants.plantTitle, sizeof(plants.plantTitle));
  memcpy(sharedSolar.plantPower, plants.plantPower, sizeof(plants.plantPower));
  sharedVersion++;
  xSemaphoreGive(lock);
}

// HTTP-Status der Antwort, negativ bei Netzwerkfehlern
static int post(uint8_t index, const char* path) {
  const int code = httpRequest(baseUrl[index] + path, "POST", nullptr);
  log_i("POST %s%s -> %d", baseUrl[index].c_str(), path, code);
  return code;
}

static bool setMode(uint8_t index, ChargeMode mode) {
  switch (mode) {
    case ChargeMode::Fast:
      return post(index, "/mode/now") == HTTP_CODE_OK;
    case ChargeMode::Smart:
    case ChargeMode::Slow: {
      const bool always = mode == ChargeMode::Slow;
      // evcc ≥ 0.316: "immer laden" + smart
      const int code = post(index, always ? "/alwayscharge/on" : "/alwayscharge/off");
      if (code == HTTP_CODE_OK) return post(index, "/mode/smart") == HTTP_CODE_OK;
      if (code < 0) return false;  // Netzwerkfehler – nicht auf die alten Modi ausweichen
      // Ältere Versionen kennen alwayscharge nicht, dafür pv/minpv
      return post(index, always ? "/mode/minpv" : "/mode/pv") == HTTP_CODE_OK;
    }
    default:
      return false;
  }
}

static bool setBoost(uint8_t index, bool on) {
  return post(index, on ? "/batteryboost/1" : "/batteryboost/0") == HTTP_CODE_OK;
}

static bool setVehicle(uint8_t index, const char* name) {
  if (name[0]) {
    const String path = "/vehicle/" + urlEncode(name);
    return post(index, path.c_str()) == HTTP_CODE_OK;
  }
  const int code = httpRequest(baseUrl[index] + "/vehicle", "DELETE", nullptr);
  log_i("DELETE %s/vehicle -> %d", baseUrl[index].c_str(), code);
  return code == HTTP_CODE_OK;
}

static bool setLimit(uint8_t index, bool energy, float value) {
  char path[32];
  if (energy)
    snprintf(path, sizeof(path), "/limitenergy/%.1f", value);
  else
    snprintf(path, sizeof(path), "/limitsoc/%d", (int)lroundf(value));
  return post(index, path) == HTTP_CODE_OK;
}

static void task(void*) {
  uint32_t lastPoll = 0;
  uint32_t nextSolar = 0;  // 0 = beim nächsten Durchlauf
  for (;;) {
    for (int c = 0; c < (int)Command::Count; c++) {
      for (uint8_t i = 0; i < lpCount; i++) {
        Slot& slot = slots[c][i];
        xSemaphoreTake(lock, portMAX_DELAY);
        const uint32_t id = slot.wantedId;
        const ChargeMode mode = slot.mode;
        const bool on = slot.on;
        char vehicle[VEHICLE_NAME_LEN];
        strlcpy(vehicle, slot.vehicle, sizeof(vehicle));
        const bool limitEnergy = slot.limitEnergy;
        const float limit = slot.limit;
        slot.wantedId = 0;
        xSemaphoreGive(lock);
        if (!id) continue;

        bool ok = false;
        if (WiFi.status() == WL_CONNECTED) {
          switch ((Command)c) {
            case Command::Mode: ok = setMode(i, mode); break;
            case Command::Boost: ok = setBoost(i, on); break;
            case Command::Vehicle: ok = setVehicle(i, vehicle); break;
            case Command::Limit: ok = setLimit(i, limitEnergy, limit); break;
            default: break;
          }
          lastPoll = 0;  // sofort neuen Zustand holen
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        slot.done.id = id;
        slot.done.result = ok ? CommandResult::Ok : CommandResult::Failed;
        slot.done.doneMs = millis();
        xSemaphoreGive(lock);
      }
    }
    if (WiFi.status() == WL_CONNECTED && (lastPoll == 0 || millis() - lastPoll >= POLL_INTERVAL_MS)) {
      lastPoll = millis() | 1;
      poll();
    }
    // Stundenwerte nur, wenn die Solarseite eingeschaltet ist; wird sie es später, sofort
    if (!settings::get().solarPage) {
      nextSolar = 0;
    } else if (WiFi.status() == WL_CONNECTED && (nextSolar == 0 || (int32_t)(millis() - nextSolar) >= 0)) {
      nextSolar = (millis() + (fetchSolar() ? SOLAR_INTERVAL_MS : SOLAR_RETRY_MS)) | 1;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void evcc::begin() {
  const Settings& s = settings::get();
  const String server = "http://" + s.evccHost + ":" + String(s.evccPort);
  serverUrl = server;
  lpCount = s.loadpointCount;

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
  char jq[1536];
  const int jqLen = snprintf(jq, sizeof(jq),
           "{lps:[.loadpoints[%s]|if . then {title,mode,alwaysCharge,chargePower,charging,connected,chargedEnergy,"
           "chargeDuration,phasesActive,sessionSolarPercentage,vehicleName,vehicleTitle,vehicleSoc,"
           "limitSoc:(.effectiveLimitSoc // .limitSoc),vehicleLimitSoc,limitEnergy,pvAction,pvRemaining,"
           "planActive,smartCostActive,batteryBoost,minCurrent:(.effectiveMinCurrent // .minCurrent),phasesConfigured,"
           "chargerPhases1p3p,chargerFeatureHeating} else null end],green:.greenShareLoadpoints,pv:.pvPower,home:.homePower,"
           "bat:((try (if (.battery.devices|length)>0 then .battery.soc else null end) catch null) // .batterySoc),"
           "batp:((try (if (.battery.devices|length)>0 then .battery.power else null end) catch null) // .batteryPower),"
           "grid:((try .grid.power catch null) // .gridPower),prio:.prioritySoc,"
           "fc:(try [.forecast.solar.today.energy,.forecast.solar.tomorrow.energy] catch null),"
           "pvs:(try [.pv[]|{t:.title,p:.power}] catch null),"
           "veh:(try [.vehicles|to_entries[]|{n:.key,t:.value.title,o:any(.value.features[]?;.==\"Offline\")}] catch [])}",
           lps.c_str());
  if (jqLen >= (int)sizeof(jq)) log_e("jq-Filter abgeschnitten (%d Zeichen)", jqLen);
  stateUrl = server + "/api/state?jq=" + urlEncode(jq);

  lock = xSemaphoreCreateMutex();
  // Netzwerk auf Kern 0, damit LVGL (loop, Kern 1) flüssig bleibt
  xTaskCreatePinnedToCore(task, "evcc", 10240, nullptr, 1, &taskHandle, 0);
}

uint8_t evcc::count() { return lpCount; }

bool evcc::latest(EvccState* out, EvccVehicles& vehicles, EvccSolar& solar) {
  xSemaphoreTake(lock, portMAX_DELAY);
  const bool changed = sharedVersion != readVersion;
  if (changed) {
    for (uint8_t i = 0; i < lpCount; i++) out[i] = shared[i];
    vehicles = sharedVehicles;
    solar = sharedSolar;
    readVersion = sharedVersion;
  }
  xSemaphoreGive(lock);
  return changed;
}

// Trägt den Befehl ein; der Aufrufer hat die Werte im Slot unter dem Lock gesetzt
static uint32_t request(Command command, uint8_t index, void (*fill)(Slot&, const void*), const void* value) {
  if (index >= lpCount) return 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  Slot& slot = slots[(int)command][index];
  fill(slot, value);
  if (++nextId == 0) nextId = 1;  // 0 heißt "nichts zu tun"
  const uint32_t id = slot.wantedId = nextId;
  xSemaphoreGive(lock);
  return id;
}

uint32_t evcc::requestMode(uint8_t index, ChargeMode mode) {
  return request(Command::Mode, index, [](Slot& s, const void* v) { s.mode = *(const ChargeMode*)v; }, &mode);
}

uint32_t evcc::requestBoost(uint8_t index, bool on) {
  return request(Command::Boost, index, [](Slot& s, const void* v) { s.on = *(const bool*)v; }, &on);
}

uint32_t evcc::requestVehicle(uint8_t index, const char* name) {
  return request(
      Command::Vehicle, index,
      [](Slot& s, const void* v) { strlcpy(s.vehicle, (const char*)v, sizeof(s.vehicle)); }, name ? name : "");
}

uint32_t evcc::requestLimit(uint8_t index, bool energy, float value) {
  struct Limit {
    bool energy;
    float value;
  } limit = {energy, value};
  return request(
      Command::Limit, index,
      [](Slot& s, const void* v) {
        s.limitEnergy = ((const Limit*)v)->energy;
        s.limit = ((const Limit*)v)->value;
      },
      &limit);
}

CommandStatus evcc::status(Command command, uint8_t index) {
  if (index >= lpCount) return CommandStatus();
  xSemaphoreTake(lock, portMAX_DELAY);
  const CommandStatus done = slots[(int)command][index].done;
  xSemaphoreGive(lock);
  return done;
}

uint32_t evcc::stackFree() { return taskHandle ? uxTaskGetStackHighWaterMark(taskHandle) : 0; }
