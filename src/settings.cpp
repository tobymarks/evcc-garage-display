#include "settings.h"

#include <Preferences.h>

#include "board.h"
#include "config.h"
#include "lang.h"

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
  s.boostButton = prefs.getBool("boost", false);
  s.brightnessPct = prefs.getUChar("bright", DEFAULT_BRIGHTNESS_PCT);
  s.vehicleSwitch = prefs.getBool("vehsw", true);
  s.chargeLimit = prefs.getBool("limit", false);
  s.solarPage = prefs.getBool("solar", false);
  s.solarSurplus = prefs.getBool("solarsur", false);
  s.solarFirst = prefs.getBool("solar1st", false);
  s.solarBig = prefs.getUChar("sbig", s.solarSurplus ? SOLAR_BIG_SURPLUS : SOLAR_BIG_PV);
  s.solarMid = prefs.getUChar("smid", SOLAR_MID_AUTO);
  s.solarTile[0] = prefs.getUChar("st1", SOLAR_TILE_TODAY);
  s.solarTile[1] = prefs.getUChar("st2", SOLAR_TILE_REST);
  s.solarTile[2] = prefs.getUChar("st3", SOLAR_TILE_TOMORROW);
  s.backlightHz = prefs.getUInt("blhz", LCD_BACKLIGHT_HZ);
  s.hostname = cleanHostname(prefs.getString("name", HOSTNAME));
  if (s.hostname.isEmpty()) s.hostname = HOSTNAME;
  s.password = prefs.getString("pw", "");
  // Ohne gespeicherte Sprache: Ein schon eingerichtetes Display bleibt deutsch, ein neues beginnt englisch
  s.language = prefs.getUChar("lang", (uint8_t)(prefs.isKey("host") ? Lang::De : Lang::En));
  prefs.end();

  if (s.rotation > 3) s.rotation = DEFAULT_ROTATION;
  if (s.theme > 2) s.theme = DEFAULT_THEME;
  s.brightnessPct = constrain(s.brightnessPct, MIN_BRIGHTNESS_PCT, 100);
  if (s.solarBig >= SOLAR_BIG_COUNT) s.solarBig = SOLAR_BIG_PV;
  if (s.solarMid >= SOLAR_MID_COUNT) s.solarMid = SOLAR_MID_AUTO;
  for (uint8_t& tile : s.solarTile)
    if (tile >= SOLAR_TILE_COUNT) tile = SOLAR_TILE_NONE;
  s.backlightHz = constrain(s.backlightHz, (uint32_t)MIN_BACKLIGHT_HZ, (uint32_t)MAX_BACKLIGHT_HZ);
  if (s.language >= (uint8_t)Lang::Count) s.language = (uint8_t)Lang::De;
  lang::set((Lang)s.language);
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
  prefs.putBool("boost", s.boostButton);
  prefs.putUChar("bright", s.brightnessPct);
  prefs.putBool("vehsw", s.vehicleSwitch);
  prefs.putBool("limit", s.chargeLimit);
  prefs.putBool("solar", s.solarPage);
  prefs.putBool("solarsur", s.solarBig == SOLAR_BIG_SURPLUS);  // für ältere Firmware nach einem Zurückrollen
  prefs.putUChar("sbig", s.solarBig);
  prefs.putUChar("smid", s.solarMid);
  prefs.putUChar("st1", s.solarTile[0]);
  prefs.putUChar("st2", s.solarTile[1]);
  prefs.putUChar("st3", s.solarTile[2]);
  prefs.putBool("solar1st", s.solarFirst);
  prefs.putUInt("blhz", s.backlightHz);
  prefs.putString("name", s.hostname);
  prefs.putString("pw", s.password);
  prefs.putUChar("lang", s.language);
  prefs.end();
}

Settings& settings::get() { return s; }

uint8_t settings::backlight() { return (uint16_t)s.brightnessPct * 255 / 100; }

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

String settings::cleanHostname(const String& text) {
  String out;
  for (size_t i = 0; i < text.length() && out.length() < MAX_HOSTNAME_LEN; i++) {
    const char c = text[i];
    if (isalnum((unsigned char)c))
      out += (char)tolower((unsigned char)c);
    else if (c == '-' || c == ' ' || c == '_' || c == '.')
      out += '-';
    else if ((uint8_t)c == 0xC3 && i + 1 < text.length()) {  // Umlaute (UTF-8) wie in Domainnamen üblich
      const uint8_t d = (uint8_t)text[++i];
      if (d == 0xA4 || d == 0x84) out += "ae";
      else if (d == 0xB6 || d == 0x96) out += "oe";
      else if (d == 0xBC || d == 0x9C) out += "ue";
      else if (d == 0x9F) out += "ss";
    }
  }
  // Keine doppelten Bindestriche, keiner am Anfang oder Ende
  while (out.indexOf("--") >= 0) out.replace("--", "-");
  while (out.startsWith("-")) out.remove(0, 1);
  if (out.length() > MAX_HOSTNAME_LEN) out.remove(MAX_HOSTNAME_LEN);
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out;
}
