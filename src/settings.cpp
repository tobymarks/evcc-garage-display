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
  s.hasTouchCal = prefs.getBytes("touch", s.touchCal, sizeof(s.touchCal)) == sizeof(s.touchCal);
  prefs.end();

  if (s.loadpoint < 1) s.loadpoint = 1;
  if (s.rotation != 1 && s.rotation != 3) s.rotation = DEFAULT_ROTATION;
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
  prefs.end();
}

void settings::saveTouchCal(const uint16_t cal[8]) {
  memcpy(s.touchCal, cal, sizeof(s.touchCal));
  s.hasTouchCal = true;
  prefs.begin("evcc", false);
  prefs.putBytes("touch", s.touchCal, sizeof(s.touchCal));
  prefs.end();
}

void settings::clearTouchCal() {
  s.hasTouchCal = false;
  prefs.begin("evcc", false);
  prefs.remove("touch");
  prefs.end();
}

Settings& settings::get() { return s; }
