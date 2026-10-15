#!/usr/bin/env bash
# Erzeugt die LVGL-Schriften – wie die EVCC-Weboberfläche:
#  - Montserrat für alle Texte (SIL Open Font License, tools/fonts/OFL-Montserrat.txt)
#  - Icons aus tools/icons: Shopicons und Material Symbols (Apache-2.0), zusammengefasst in evcc-icons.ttf
# Aufruf aus dem Projektverzeichnis: ./tools/gen_fonts.sh (braucht fontforge und npx)
set -euo pipefail
cd "$(dirname "$0")/.."

FONTS=tools/fonts
OUT=src/fonts
CONV="npx -y lv_font_conv@1.5.3"

# ASCII + deutsche Umlaute, °, ·, Ø, –, …
TEXT_RANGE="0x20-0x7E,0xB0,0xB7,0xC4,0xD6,0xD8,0xDC,0xDF,0xE4,0xF6,0xFC,0x2013,0x2026"
# Alle Icons der evcc-icons.ttf, siehe tools/build_icon_font.py und src/icons.h
ICON_RANGE="0xE000-0xE008"

fontforge -quiet -lang=py -script tools/build_icon_font.py

gen_text() { # name size weight
  $CONV --bpp 4 --size "$2" --no-compress --format lvgl --lv-include lvgl.h \
    --font "$FONTS/Montserrat-$3.ttf" -r "$TEXT_RANGE" \
    --font "$FONTS/evcc-icons.ttf" -r "$ICON_RANGE" \
    --lv-font-name "$1" -o "$OUT/$1.c"
}

# Größen für das 4"-Display (480x480)
gen_text font_16 16 Medium
gen_text font_18 18 Medium
gen_text font_22 22 Medium
gen_text font_24 24 SemiBold
gen_text font_26 26 Bold
gen_text font_32 32 Bold

# Große Ziffern für die Ladeleistung (fett wie die Werte in EVCC)
$CONV --bpp 4 --size 112 --no-compress --format lvgl --lv-include lvgl.h \
  --font "$FONTS/Montserrat-Bold.ttf" -r "0x20,0x2C-0x2E,0x30-0x39,0x2013" \
  --lv-font-name font_big -o "$OUT/font_big.c"

# Große Icons ("Kein Fahrzeug", Blitz neben der Ladeleistung, Hinweisbildschirm)
$CONV --bpp 4 --size 72 --no-compress --format lvgl --lv-include lvgl.h \
  --font "$FONTS/evcc-icons.ttf" -r "$ICON_RANGE" \
  --lv-font-name font_icon_big -o "$OUT/font_icon_big.c"

echo "Schriften erzeugt in $OUT"
