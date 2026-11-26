#pragma once

#include <cstdio>

namespace co2 {
constexpr double UNSET = -1;
constexpr int MAX_VALUE = 9999;  // Eingabebereich des Displays in g/kWh

struct Limit {
  bool supported;
  double value;
  Limit(bool supported = false, double value = UNSET) : supported(supported), value(value) {}
};

inline bool confirmed(Limit readback, int target) { return readback.supported && readback.value == target; }

inline bool prepareWrite(char* path, size_t len, Limit fresh, double before, int target) {
  if (!fresh.supported || fresh.value != before || target < 0 || target > MAX_VALUE || confirmed(fresh, target))
    return false;
  const int n = snprintf(path, len, "/smartcostlimit/%d", target);
  return n > 0 && static_cast<size_t>(n) < len;
}
}  // namespace co2
