#pragma once

#include <cstddef>

namespace pv {
constexpr size_t MAX_PLANTS = 3;
using MeterId = char[96];
struct Peak {
  MeterId id;
  float kwp;
};
using Peaks = Peak[MAX_PLANTS];

bool copyId(MeterId& out, const char* id, size_t length);
bool uniqueMeters(const MeterId* ids, size_t count, size_t total);
float peakFor(const Peaks& peaks, const char* id);
bool parseKwp(const char* text, float& kwp);  // blank clears the entry

// Returns true for a complete absolute scale, false for the relative scale.
bool scaleBars(const MeterId* ids, const float* watts, size_t count, size_t total, const Peaks& peaks, float* fractions);
}  // namespace pv
