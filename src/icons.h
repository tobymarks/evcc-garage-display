#pragma once

// Icons wie in der evcc-Weboberfläche (Shopicons, Material Symbols) als UTF-8 im privaten Bereich ab U+E000.
// In font_16 … font_32 und font_icon_big enthalten – siehe tools/build_icon_font.py und tools/gen_fonts.sh
#define ICON_SUN "\xEE\x80\x80"
#define ICON_BOLT "\xEE\x80\x81"
#define ICON_PLUG "\xEE\x80\x82"  // Kabel mit Stecker – evcc zeigt es bei "Kein Fahrzeug"
#define ICON_CAR "\xEE\x80\x83"
#define ICON_ALERT "\xEE\x80\x84"
#define ICON_SETTINGS "\xEE\x80\x85"
#define ICON_ALWAYS "\xEE\x80\x86"  // ∞ – "Immer laden" in evcc
#define ICON_WIFI_OFF "\xEE\x80\x87"
#define ICON_CLOUD_OFF "\xEE\x80\x88"
#define ICON_BATTERY "\xEE\x80\x89"  // Hausakku
