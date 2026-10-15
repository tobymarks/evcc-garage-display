#!/usr/bin/env bash
# Rendert Zustände der Oberfläche als PNG (480 × 480, hell und dunkel) – ohne Gerät.
# Aufruf aus dem Projektverzeichnis: ./tools/sim/render.sh [Ausgabeordner] [Zustand …]
# Standard: alle Zustände der Website nach site/img. Braucht clang und macOS (sips) bzw. ImageMagick.
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT="${1:-site/img}"
shift || true
SCENES=("${@:-charging solar fast start nocar stale}")
LVGL=.pio/libdeps/panel/lvgl
BUILD=.pio/sim
[ -d "$LVGL" ] || { echo "LVGL fehlt – einmal 'pio run -e panel' ausführen"; exit 1; }
mkdir -p "$BUILD/obj" "$OUT"

FLAGS=(-O1 -w -DLV_CONF_INCLUDE_SIMPLE -Iinclude -I"$LVGL" -Isrc -Itools/sim/stub)
# LVGL nur einmal übersetzen
if [ ! -f "$BUILD/liblvgl.a" ]; then
  export BUILD
  find "$LVGL/src" -name '*.c' -print0 |
    xargs -0 -P 8 -I{} sh -c 'clang -c "$@" "{}" -o "$BUILD/obj/$(echo "{}" | tr /. __).o"' _ "${FLAGS[@]}"
  ar rcs "$BUILD/liblvgl.a" "$BUILD"/obj/*.o
fi
# Schriften als C übersetzen (in C++ wären die const-Variablen nicht von außen sichtbar)
for f in src/fonts/*.c; do clang -c "${FLAGS[@]}" "$f" -o "$BUILD/$(basename "$f" .c).o"; done
clang++ -std=c++17 "${FLAGS[@]}" tools/sim/sim.cpp src/ui.cpp src/preview.cpp "$BUILD"/font_*.o \
  "$BUILD/liblvgl.a" -o "$BUILD/sim"

TMP=$(mktemp -d)
# shellcheck disable=SC2086
"$BUILD/sim" "$TMP" ${SCENES[*]}
for f in "$TMP"/*.bmp; do
  png="$OUT/$(basename "${f%.bmp}").png"
  if command -v sips >/dev/null; then sips -s format png "$f" --out "$png" >/dev/null; else convert "$f" "$png"; fi
  echo "$png"
done
rm -rf "$TMP"
