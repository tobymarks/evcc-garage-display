#include "settings.h"

#include <Preferences.h>

#include "config.h"

static Settings s;
static Preferences prefs;

void settings::load() {
  prefs.begin("evcc", false);  // schreibbar öffnen, legt den Namespace beim ersten Start an
  s.evccHost = prefs.getString("host", DEFAULT_EVCC_HOST);
  s.evccPort = prefs.getUShort("port", DEFAULT_EVCC_PORT);
  s.loadpoint = prefs.getUChar("lp", DEFAULT_LOADPOINT);
  s.latitude = prefs.getFloat("lat", DEFAULT_LATITUDE);
  s.longitude = prefs.getFloat("lon", DEFAULT_LONGITUDE);
  s.rotation = prefs.getUChar("rot", DEFAULT_ROTATION);
  s.theme = prefs.getUChar("theme", DEFAULT_THEME);
  s.screenTimeoutMin = prefs.getUChar("timeout", DEFAULT_SCREEN_TIMEOUT_MIN);
  s.autoUpdate = prefs.getBool("autoupd", DEFAULT_AUTO_UPDATE);
  prefs.end();

  if (s.loadpoint < 1) s.loadpoint = 1;
  if (s.rotation > 3) s.rotation = DEFAULT_ROTATION;
  if (s.theme > 2) s.theme = DEFAULT_THEME;
}

void settings::save() {
  prefs.begin("evcc", false);
  prefs.putString("host", s.evccHost);
  prefs.putUShort("port", s.evccPort);
  prefs.putUChar("lp", s.loadpoint);
  prefs.putFloat("lat", s.latitude);
  prefs.putFloat("lon", s.longitude);
  prefs.putUChar("rot", s.rotation);
  prefs.putUChar("theme", s.theme);
  prefs.putUChar("timeout", s.screenTimeoutMin);
  prefs.putBool("autoupd", s.autoUpdate);
  prefs.end();
}

Settings& settings::get() { return s; }
