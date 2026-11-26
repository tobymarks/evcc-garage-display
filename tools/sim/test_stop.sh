#!/usr/bin/env bash
# Host-only touch and layout tests; pass an unpacked LVGL 9.2.2 source directory.
set -euo pipefail
LVGL=$(cd "${1:?Usage: bash tools/sim/test_stop.sh /path/to/lvgl-9.2.2 [build-dir]}" && pwd)
cd "$(dirname "$0")/../.."
BUILD=${2:-.pio/test-stop}
mkdir -p "$BUILD"
printf '#define FW_VERSION "host-test"\n' > "$BUILD/fw_version.h"
cmake -S "$LVGL" -B "$BUILD/lvgl" -DLV_CONF_PATH="$PWD/include/lv_conf.h" \
  -DLV_CONF_BUILD_DISABLE_THORVG_INTERNAL=ON -DLV_CONF_BUILD_DISABLE_EXAMPLES=ON \
  -DLV_CONF_BUILD_DISABLE_DEMOS=ON
cmake --build "$BUILD/lvgl" --target lvgl --parallel 4
flags=(-O1 -DLV_CONF_INCLUDE_SIMPLE -Iinclude -I"$LVGL" -Isrc -Itools/sim/stub -I"$BUILD")
fonts=()
for src in src/fonts/*.c; do
  obj="$BUILD/$(basename "$src" .c).o"
  "${CC:-cc}" "${flags[@]}" -c "$src" -o "$obj"
  fonts+=("$obj")
done
for test in stop solar_spacing; do
  "${CXX:-c++}" -std=c++17 "${flags[@]}" "tools/sim/test_${test}.cpp" \
    src/ui.cpp src/lang.cpp src/lang_en.cpp src/pv_scaling.cpp \
    "${fonts[@]}" "$BUILD/lvgl/lib/liblvgl.a" -o "$BUILD/test-$test"
  "$BUILD/test-$test"
done
