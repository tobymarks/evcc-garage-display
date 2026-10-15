#include "updater.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "fw_version.h"
#include "settings.h"

// Stammzertifikate aus ESP-IDF, damit die Verbindung zu GitHub geprüft wird
extern const uint8_t crtBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t crtBundleEnd[] asm("_binary_x509_crt_bundle_end");

enum class Job : uint8_t { None, Check, Install };

// Die Netzwerkarbeit läuft in einem eigenen Task; der loop liest die Ergebnisse nur, wenn er fertig ist
static TaskHandle_t taskHandle;
static volatile Job job = Job::None;
static volatile bool busy = false;
static volatile bool installOk = false;
static volatile bool installFinished = false;

static char available[40] = "";
static String binUrl;
static String binMd5;
static int binSize = 0;
static char lastResult[80] = "noch nicht geprüft";
static uint32_t nextCheck = UPDATE_FIRST_CHECK_MS;
static bool forceInstall = false;
static bool confirmed = false;

// Arduino bestätigt neue Firmware sonst sofort beim Start; wir erst nach UPDATE_CONFIRM_MS (siehe loop)
bool verifyRollbackLater() { return true; }

static void begin(HTTPClient& http, NetworkClientSecure& client, const String& url) {
  client.setCACertBundle(crtBundleStart, crtBundleEnd - crtBundleStart);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.begin(client, url);
}

static void check() {
  NetworkClientSecure client;
  HTTPClient http;
  begin(http, client, UPDATE_BASE_URL "update.json");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(lastResult, sizeof(lastResult), "Prüfung fehlgeschlagen (HTTP %d)", code);
    http.end();
    return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  const char* version = doc["version"] | "";
  if (err || !*version || !doc["file"].is<const char*>()) {
    snprintf(lastResult, sizeof(lastResult), "update.json unlesbar");
    return;
  }
  if (!strcmp(version, FW_VERSION)) {
    available[0] = '\0';
    snprintf(lastResult, sizeof(lastResult), "aktuell");
    return;
  }
  strlcpy(available, version, sizeof(available));
  binUrl = String(UPDATE_BASE_URL) + (const char*)doc["file"];
  binMd5 = doc["md5"] | "";
  binSize = doc["size"] | 0;
  snprintf(lastResult, sizeof(lastResult), "neue Version %s verfügbar", available);
}

static bool install() {
  NetworkClientSecure client;
  HTTPClient http;
  begin(http, client, binUrl);
  const int code = http.GET();
  const int len = http.getSize();
  if (code != HTTP_CODE_OK || len <= 0 || (binSize && len != binSize)) {
    snprintf(lastResult, sizeof(lastResult), "Download fehlgeschlagen (HTTP %d, %d Byte)", code, len);
    http.end();
    return false;
  }
  if (!Update.begin(len, U_FLASH)) {
    snprintf(lastResult, sizeof(lastResult), "Update nicht möglich: %s", Update.errorString());
    http.end();
    return false;
  }
  if (binMd5.length() == 32) Update.setMD5(binMd5.c_str());  // falsche Prüfsumme → end() schlägt fehl
  const size_t written = Update.writeStream(*http.getStreamPtr());
  http.end();
  if (written != (size_t)len || !Update.end()) {
    snprintf(lastResult, sizeof(lastResult), "Update abgebrochen: %s", Update.errorString());
    Update.abort();
    return false;
  }
  snprintf(lastResult, sizeof(lastResult), "%s installiert", available);
  return true;
}

static void task(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const Job j = job;
    if (j == Job::Check) {
      check();
    } else if (j == Job::Install) {
      installOk = install();
      installFinished = true;
    }
    job = Job::None;
    busy = false;
  }
}

static void start(Job j) {
  if (busy || WiFi.status() != WL_CONNECTED) return;
  busy = true;
  job = j;
  xTaskNotifyGive(taskHandle);
}

void updater::begin() {
  // TLS braucht viel Stack; Kern 0 wie das evcc-Polling, damit die Oberfläche flüssig bleibt
  xTaskCreatePinnedToCore(task, "update", 16384, nullptr, 1, &taskHandle, 0);
  log_i("Firmware %s", FW_VERSION);
}

updater::Event updater::loop(bool idle) {
  // Neue Firmware läuft stabil → behalten (sonst stellt der Bootloader beim nächsten Neustart die alte wieder her)
  if (!confirmed && millis() > UPDATE_CONFIRM_MS) {
    confirmed = true;
    esp_ota_mark_app_valid_cancel_rollback();
  }

  if (installFinished) {
    installFinished = false;
    forceInstall = false;
    if (installOk) return Event::Done;
    available[0] = '\0';  // erst nach der nächsten Prüfung erneut versuchen
    nextCheck = millis() + UPDATE_CHECK_INTERVAL_MS;
    return Event::Failed;
  }
  if (busy) return Event::None;

  if ((int32_t)(millis() - nextCheck) >= 0) {
    nextCheck = millis() + UPDATE_CHECK_INTERVAL_MS;
    start(Job::Check);
    return Event::None;
  }
  if (available[0] && (forceInstall || (settings::get().autoUpdate && idle))) {
    start(Job::Install);
    if (busy) return Event::Installing;
  }
  return Event::None;
}

void updater::checkNow() { nextCheck = millis(); }

void updater::installNow() { forceInstall = true; }

const char* updater::availableVersion() { return available; }

String updater::status() {
  String s = "Firmware " FW_VERSION "\n";
  s += "Automatische Updates: ";
  s += settings::get().autoUpdate ? "an\n" : "aus\n";
  s += "Letzte Prüfung: ";
  s += !busy ? lastResult : job == Job::Install ? "Installation läuft …" : "läuft …";
  s += "\n";
  return s;
}
