#!/usr/bin/env bash
# Erzeugt die LVGL-Schriften:
#  - Inter für UI-Texte (auf dem 320x240-Display bei kleinen Größen besser lesbar)
#  - Montserrat SemiBold für die große Leistungszahl (Schrift der EVCC-Weboberfläche)
#  - Tabler-Icons
# Aufruf aus dem Projektverzeichnis: ./tools/gen_fonts.sh
set -euo pipefail
cd "$(dirname "$0")/.."

FONTS=tools/fonts
# tabler-icons-subset.ttf: nur die benötigten Icons, ohne GSUB (lv_font_conv kann die Original-Datei nicht lesen)
OUT=src/fonts
CONV="npx -y lv_font_conv@1.5.3"

# ASCII + deutsche Umlaute, °, ·, Ø, –, …
TEXT_RANGE="0x20-0x7E,0xB0,0xB7,0xC4,0xD6,0xD8,0xDC,0xDF,0xE4,0xF6,0xFC,0x2013,0x2026"
# Tabler: sun, sparkles, bolt, wifi-off, car, car-off, plug-connected, alert-triangle, settings, hand-finger
ICON_RANGE="0xEB30,0xF6D7,0xEA38,0xECFA,0xEBBB,0xF0C7,0xF00A,0xEA06,0xEB20,0xEE94"

gen_text() { # name size weight
  $CONV --bpp 4 --size "$2" --no-compress --format lvgl --lv-include lvgl.h \
    --font "$FONTS/Inter-$3.ttf" -r "$TEXT_RANGE" \
    --font "$FONTS/tabler-icons-subset.ttf" -r "$ICON_RANGE" \
    --lv-font-name "$1" -o "$OUT/$1.c"
}

# Größen für das 4"-Display (480x480)
gen_text font_18 18 Medium
gen_text font_22 22 Medium
gen_text font_24 24 SemiBold
gen_text font_26 26 SemiBold
gen_text font_32 32 SemiBold

# Große Ziffern für die Ladeleistung
$CONV --bpp 4 --size 112 --no-compress --format lvgl --lv-include lvgl.h \
  --font "$FONTS/Montserrat-SemiBold.ttf" -r "0x20,0x2C-0x2E,0x30-0x39,0x2013" \
  --lv-font-name font_big -o "$OUT/font_big.c"

# Großes Icon für "kein Fahrzeug"
$CONV --bpp 4 --size 72 --no-compress --format lvgl --lv-include lvgl.h \
  --font "$FONTS/tabler-icons-subset.ttf" -r "0xF0C7,0xEBBB,0xECFA,0xEA06,0xEE94,0xEB20,0xF00A" \
  --lv-font-name font_icon_big -o "$OUT/font_icon_big.c"

echo "Schriften erzeugt in $OUT"
