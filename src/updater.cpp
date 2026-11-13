#include "updater.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <MD5Builder.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

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

// Installierte Version, die danach nicht lief (bleibt über Neustarts erhalten): Auf manchen Displays
// startet der Bootloader nach dem Update wieder die alte Firmware. Ohne Zähler lüde das Display
// dieselbe Version dann alle paar Minuten erneut und startete jedes Mal neu.
static char triedVersion[sizeof(available)] = "";
static volatile uint8_t triedCount = 0;     // so oft installiert
static volatile uint8_t triedReadback = 0;  // … davon stand die Firmware nachweislich richtig im Flash

static uint8_t failedAttempts() { return available[0] && !strcmp(triedVersion, available) ? triedCount : 0; }

static void clearAttempts() {
  if (!triedCount) return;
  triedVersion[0] = '\0';
  triedCount = 0;
  triedReadback = 0;
  Preferences prefs;
  prefs.begin("update", false);
  prefs.clear();
  prefs.end();
}

static void loadAttempts() {
  Preferences prefs;
  prefs.begin("update", false);
  if (prefs.isKey("ver")) prefs.getString("ver", triedVersion, sizeof(triedVersion));
  triedCount = prefs.getUChar("n", 0);
  triedReadback = prefs.getUChar("ok", 0);
  prefs.end();
  if (!strcmp(triedVersion, FW_VERSION)) clearAttempts();  // die installierte Version läuft jetzt: erledigt
}

static void noteAttempt(bool readbackOk) {
  if (strcmp(triedVersion, available)) {
    strlcpy(triedVersion, available, sizeof(triedVersion));
    triedCount = 0;
    triedReadback = 0;
  }
  if (triedCount < UINT8_MAX) {
    triedCount = triedCount + 1;
    if (readbackOk) triedReadback = triedReadback + 1;
  }
  Preferences prefs;
  prefs.begin("update", false);
  prefs.putString("ver", triedVersion);
  prefs.putUChar("n", triedCount);
  prefs.putUChar("ok", triedReadback);
  prefs.end();
}

// Liest die geschriebene Firmware direkt vom Flash-Baustein zurück. Update.end() prüft sie nur über den
// Flash-Cache; das hier zeigt, ob wirklich im Flash steht, was heruntergeladen wurde.
static bool flashMatches(const esp_partition_t* partition, size_t len, const String& md5) {
  constexpr size_t BLOCK = 4096;
  uint8_t* buf = (uint8_t*)ps_malloc(BLOCK);  // PSRAM: der interne RAM ist knapp
  if (!buf) return false;
  MD5Builder builder;
  builder.begin();
  bool ok = true;
  for (size_t offset = 0; ok && offset < len;) {
    const size_t n = min(BLOCK, len - offset);
    ok = esp_partition_read(partition, offset, buf, n) == ESP_OK;
    builder.add(buf, n);
    offset += n;
    if (offset % (64 * BLOCK) == 0) vTaskDelay(1);  // andere Tasks auf diesem Kern nicht aushungern
  }
  free(buf);
  builder.calculate();
  return ok && md5.equalsIgnoreCase(builder.toString());
}

// Arduino bestätigt neue Firmware sonst sofort beim Start; wir erst nach UPDATE_CONFIRM_MS (siehe loop).
// extern "C": der Kern sucht die Funktion unter ihrem C-Namen, sonst greift seine eigene (false).
extern "C" bool verifyRollbackLater() { return true; }

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
    clearAttempts();  // z. B. per USB auf den veröffentlichten Stand gebracht: frühere Fehlversuche sind überholt
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
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);  // dorthin schreibt Update
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
  // Ohne Prüfsumme lässt sich nichts vergleichen; das zählt dann nicht als Fehler
  const bool readbackOk = !target || binMd5.length() != 32 || flashMatches(target, len, binMd5);
  noteAttempt(readbackOk);
  snprintf(lastResult, sizeof(lastResult), "%s installiert%s", available,
           readbackOk ? "" : " (Flash-Inhalt weicht ab)");
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
  loadAttempts();
  xTaskCreatePinnedToCore(task, "update", 16384, nullptr, 1, &taskHandle, 0);
  log_i("Firmware %s", FW_VERSION);
}

updater::Event updater::loop(bool idle) {
  // Neue Firmware läuft stabil → behalten (sonst stellt der Bootloader beim nächsten Neustart die alte wieder her)
  // Ohne WLAN käme kein weiteres Update mehr an – dann lieber beim nächsten Neustart zurück zur alten.
  if (millis() > UPDATE_CONFIRM_MS && WiFi.status() == WL_CONNECTED) confirm();

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
  // Lief eine Version nach mehreren Installationen nicht, nur noch selten erneut versuchen:
  // Jeder Fehlversuch endet mit einem Neustart, die Laufzeit zählt also seit dem letzten.
  const bool retryDue = failedAttempts() < UPDATE_FAST_ATTEMPTS || millis() >= UPDATE_RETRY_AFTER_MS;
  if (available[0] && (forceInstall || (settings::get().autoUpdate && idle && retryDue))) {
    start(Job::Install);
    if (busy) return Event::Installing;
  }
  // "Sofort installieren" gilt nur für eine jetzt bekannte Version, nicht für spätere
  if (!available[0]) forceInstall = false;
  return Event::None;
}

void updater::checkNow() { nextCheck = millis(); }

bool updater::working() { return busy; }

void updater::confirm() {
  if (confirmed) return;
  confirmed = true;
  // esp_ota_mark_app_valid_cancel_rollback() bestätigt den zuletzt gesetzten Starteintrag, nicht die
  // laufende Firmware. Hat der Bootloader nach einem Update doch wieder die alte gestartet, würde sie
  // sonst die neue als gut markieren, und deren Rückfallschutz wäre weg.
  if (esp_ota_get_running_partition() != esp_ota_get_boot_partition()) return;
  esp_ota_mark_app_valid_cancel_rollback();
}

void updater::installNow() { forceInstall = true; }

const char* updater::availableVersion() { return available; }

String updater::status() {
  String s = "Firmware " FW_VERSION "\n";
  s += "Automatische Updates: ";
  s += settings::get().autoUpdate ? "an\n" : "aus\n";
  s += "Letzte Prüfung: ";
  s += !busy ? lastResult : job == Job::Install ? "Installation läuft …" : "läuft …";
  s += "\n";
  s += failureNote();
  return s;
}

String updater::failureNote() {
  if (!triedCount) return String();
  char buf[240];
  snprintf(buf, sizeof(buf),
           "Version %s wurde %u-mal installiert, lief danach aber nicht (Flash-Inhalt nach dem Schreiben "
           "%u-mal richtig).\n",
           triedVersion, triedCount, triedReadback);
  String s = buf;
  if (failedAttempts() >= UPDATE_FAST_ATTEMPTS && settings::get().autoUpdate) {
    const uint32_t now = millis();
    if (now >= UPDATE_RETRY_AFTER_MS)
      s += "Nächster automatischer Versuch, sobald das Display 10 Minuten nicht gebraucht wird";
    else
      s += "Nächster automatischer Versuch in etwa " + String((UPDATE_RETRY_AFTER_MS - now) / 3600000UL + 1) + " h";
    s += "; sofort mit /update?check=1&install=1\n";
  }
  return s;
}
