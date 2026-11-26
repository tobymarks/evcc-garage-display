// Host test: g++ -std=c++11 -Wall -Wextra -Werror -Isrc tests/pv_scaling.cpp src/pv_scaling.cpp -o "$TMPDIR/pv_scaling" && "$TMPDIR/pv_scaling"
#include "pv_scaling.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>

int main() {
  const pv::MeterId ids[pv::MAX_PLANTS] = {"west", "east"};
  const float watts[] = {1300, 2600};
  float fractions[pv::MAX_PLANTS];
  const pv::Peaks empty = {};
  assert(!pv::scaleBars(ids, watts, 2, 2, empty, fractions));
  assert(fractions[0] == 0.5f && fractions[1] == 1.0f);

  pv::Peaks peaks = {{"east", 2}, {"west", 9}};
  assert(pv::scaleBars(ids, watts, 2, 2, peaks, fractions));
  assert(std::fabs(fractions[0] - 1.3f / 9) < 0.00001f && fractions[1] == 1.0f);
  const pv::MeterId reordered[pv::MAX_PLANTS] = {"east", "west"};
  assert(pv::scaleBars(reordered, watts, 2, 2, peaks, fractions));
  assert(std::fabs(fractions[0] - 0.65f) < 0.00001f);
  assert(std::fabs(fractions[1] - 2.6f / 9) < 0.00001f);

  const auto expectRelative = [&](const pv::MeterId* meters, size_t total) {
    assert(!pv::scaleBars(meters, watts, 2, total, peaks, fractions));
    assert(fractions[0] == 0.5f && fractions[1] == 1.0f);
  };
  expectRelative(ids, 4);  // grouped row cannot have a unique meter ID
  const pv::MeterId duplicate[pv::MAX_PLANTS] = {"west", "west"};
  expectRelative(duplicate, 2);
  const pv::MeterId missing[pv::MAX_PLANTS] = {"west", ""};
  expectRelative(missing, 2);
  const pv::MeterId replaced[pv::MAX_PLANTS] = {"west", "south"};
  expectRelative(replaced, 2);
  const float prior = peaks[0].kwp;
  for (const float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    peaks[0].kwp = invalid;
    expectRelative(ids, 2);
  }
  peaks[0].kwp = prior;
  peaks[2] = peaks[0];
  expectRelative(ids, 2);  // duplicate saved mapping, even with identical values

  pv::MeterId id;
  assert(pv::copyId(id, "roof-1", 6) && !std::strcmp(id, "roof-1"));
  assert(!pv::copyId(id, "roof\0x", 6) && !id[0]);
  char longId[sizeof(id) + 1];
  std::memset(longId, 'x', sizeof(id));
  longId[sizeof(id)] = '\0';
  assert(!pv::copyId(id, longId, sizeof(id)) && !id[0]);

  float kwp = -1;
  assert(pv::parseKwp("", kwp) && kwp == 0);
  assert(pv::parseKwp("9,2", kwp) && kwp == 9.2f);
  assert(pv::parseKwp("9.2", kwp) && kwp == 9.2f);
  assert(pv::parseKwp("1e-5", kwp) && kwp == 1e-5f);
  for (const char* invalid : {"nan", "inf", "0", "-9", "9 kWp", "1,2.3", "0x1", "1e100", "1e-100"})
    assert(!pv::parseKwp(invalid, kwp));
}
