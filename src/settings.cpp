#include "settings.h"

#include <Preferences.h>

#include "config.h"

static Settings s;
static Preferences prefs;

void settings::load() {
  prefs.begin("evcc", false);  // schreibbar öffnen, legt den Namespace beim ersten Start an
  s.evccHost = prefs.getString("host", DEFAULT_EVCC_HOST);
  s.evccPort = prefs.getUShort("port", DEFAULT_EVCC_PORT);
  // Früher gab es nur einen Ladepunkt ("lp"); er bleibt der Standard für die neue Liste
  const String lps = prefs.getString("lps", String(prefs.getUChar("lp", DEFAULT_LOADPOINT)));
  parseLoadpoints(lps.c_str(), s);
  s.latitude = prefs.getFloat("lat", DEFAULT_LATITUDE);
  s.longitude = prefs.getFloat("lon", DEFAULT_LONGITUDE);
  s.rotation = prefs.getUChar("rot", DEFAULT_ROTATION);
  s.theme = prefs.getUChar("theme", DEFAULT_THEME);
  s.screenTimeoutMin = prefs.getUChar("timeout", DEFAULT_SCREEN_TIMEOUT_MIN);
  s.autoUpdate = prefs.getBool("autoupd", DEFAULT_AUTO_UPDATE);
  prefs.end();

  if (s.rotation > 3) s.rotation = DEFAULT_ROTATION;
  if (s.theme > 2) s.theme = DEFAULT_THEME;
}

void settings::save() {
  prefs.begin("evcc", false);
  prefs.putString("host", s.evccHost);
  prefs.putUShort("port", s.evccPort);
  prefs.putString("lps", loadpointsText(s));
  prefs.putFloat("lat", s.latitude);
  prefs.putFloat("lon", s.longitude);
  prefs.putUChar("rot", s.rotation);
  prefs.putUChar("theme", s.theme);
  prefs.putUChar("timeout", s.screenTimeoutMin);
  prefs.putBool("autoupd", s.autoUpdate);
  prefs.end();
}

Settings& settings::get() { return s; }

void settings::parseLoadpoints(const char* text, Settings& out) {
  out.loadpointCount = 0;
  for (const char* p = text; *p && out.loadpointCount < MAX_LOADPOINTS;) {
    if (!isdigit((unsigned char)*p)) {
      p++;
      continue;
    }
    char* end;
    const long n = strtol(p, &end, 10);
    p = end;
    if (n < 1 || n > 99) continue;
    bool seen = false;
    for (int i = 0; i < out.loadpointCount; i++) seen |= out.loadpoints[i] == n;
    if (!seen) out.loadpoints[out.loadpointCount++] = n;
  }
  if (out.loadpointCount == 0) out.loadpoints[out.loadpointCount++] = DEFAULT_LOADPOINT;
}

String settings::loadpointsText(const Settings& in) {
  String t;
  for (int i = 0; i < in.loadpointCount; i++) {
    if (i) t += ',';
    t += in.loadpoints[i];
  }
  return t;
}
