#include "pv_scaling.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

bool pv::copyId(MeterId& out, const char* id, size_t length) {
  out[0] = '\0';
  if (!id || length == 0 || length >= sizeof(out)) return false;
  for (size_t i = 0; i < length; ++i) {
    const auto c = static_cast<unsigned char>(id[i]);
    if (c < 0x20 || c == 0x7f) return false;
  }
  std::memcpy(out, id, length);
  out[length] = '\0';
  return true;
}

bool pv::uniqueMeters(const MeterId* ids, size_t count, size_t total) {
  // Bei einer Sammelzeile gibt es keine Zuordnung zu einer einzelnen Anlage.
  if (count == 0 || count > MAX_PLANTS || count != total) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!ids[i][0]) return false;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(ids[i], ids[j]) == 0) return false;
  }
  return true;
}

float pv::peakFor(const Peaks& peaks, const char* id) {
  if (!id[0]) return 0;
  float match = 0;
  for (const Peak& peak : peaks) {
    if (std::strncmp(peak.id, id, sizeof(peak.id)) != 0) continue;
    if (match || !std::isfinite(peak.kwp) || peak.kwp <= 0) return 0;
    match = peak.kwp;
  }
  return match;
}

bool pv::parseKwp(const char* text, float& kwp) {
  if (!*text) {
    kwp = 0;
    return true;
  }
  char number[32];
  const size_t length = std::strlen(text);
  if (length >= sizeof(number)) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = text[i] == ',' ? '.' : text[i];
    if (!std::strchr("0123456789.eE+-", c)) return false;
    number[i] = c;
  }
  number[length] = '\0';
  char* end;
  const float value = std::strtof(number, &end);
  if (*end || !std::isfinite(value) || value <= 0) return false;
  kwp = value;
  return true;
}

static float nonnegativeWatts(float watts) {
  return std::isfinite(watts) && watts > 0 ? watts : 0;
}

bool pv::scaleBars(const MeterId* ids, const float* watts, size_t count, size_t total, const Peaks& peaks, float* fractions) {
  bool absolute = uniqueMeters(ids, count, total);
  float kwp[MAX_PLANTS] = {};
  for (size_t i = 0; absolute && i < count; ++i) {
    kwp[i] = peakFor(peaks, ids[i]);
    absolute = kwp[i] > 0;
  }
  float maxWatts = 1;  // wie zuvor: schützt bei Stillstand vor Division durch null
  if (!absolute)
    for (size_t i = 0; i < count; ++i) maxWatts = std::fmax(maxWatts, nonnegativeWatts(watts[i]));
  for (size_t i = 0; i < count; ++i) {
    const float power = nonnegativeWatts(watts[i]);
    fractions[i] = absolute ? std::fmin(1.0, power / (1000.0 * kwp[i])) : power / maxWatts;
  }
  return absolute;
}
