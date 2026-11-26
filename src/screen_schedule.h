#pragma once

#include <stdint.h>
#include <string.h>
#include <time.h>

namespace screen {
constexpr int DAY_MINUTES = 24 * 60;
enum class Mode { Legacy, Inside, Outside };

struct Window {
  bool enabled;
  uint16_t start;
  uint16_t end;

  constexpr Window(bool enabled = false, uint16_t start = 8 * 60, uint16_t end = 22 * 60)
      : enabled(enabled), start(start), end(end) {}

  Mode mode(int minute) const {
    if (!enabled || !validTimes() || minute < 0 || minute >= DAY_MINUTES)
      return Mode::Legacy;
    const bool inside = start < end ? minute >= start && minute < end : minute >= start || minute < end;
    return inside ? Mode::Inside : Mode::Outside;
  }

  Mode modeAt(time_t now, bool clockValid) const {
    struct tm local;
    if (!enabled || !clockValid || !localtime_r(&now, &local)) return Mode::Legacy;
    return mode(local.tm_hour * 60 + local.tm_min);
  }

  bool fromForm(const char* on, const char* startText, const char* endText) {
    if (!on || (strcmp(on, "0") && strcmp(on, "1"))) return false;
    const Window candidate{on[0] == '1', parseTime(startText), parseTime(endText)};
    if (!candidate.validTimes()) return false;
    *this = candidate;
    return true;
  }

  // One NVS value: enabling the window and changing the pair is a single write.
  uint32_t store() const {
    return (enabled ? 0x80000000UL : 0) | (uint32_t(start) << 16) | end;
  }

  static Window fromStored(uint32_t value) {
    const Window candidate{(value >> 31) != 0, uint16_t((value >> 16) & 0x7fff), uint16_t(value)};
    return candidate.validTimes() ? candidate : Window{};
  }

 private:
  bool validTimes() const { return start < DAY_MINUTES && end < DAY_MINUTES && start != end; }

  static uint16_t parseTime(const char* text) {
    if (!text || strlen(text) != 5 || text[2] != ':') return DAY_MINUTES;
    for (int i = 0; i < 5; i++)
      if (i != 2 && (text[i] < '0' || text[i] > '9')) return DAY_MINUTES;
    const int h = (text[0] - '0') * 10 + text[1] - '0';
    const int m = (text[3] - '0') * 10 + text[4] - '0';
    return h < 24 && m < 60 ? h * 60 + m : DAY_MINUTES;
  }
};

class Power {
 public:
  void sleep(uint32_t legacyActivity) {
    forcedSleep_ = true;
    sleepActivity_ = legacyActivity;
    temporaryWake_ = false;
  }

  bool isForcedAsleep() const { return forcedSleep_; }

  bool keepOn(Mode mode, uint32_t now, uint32_t legacyActivity, uint8_t timeoutMin, bool charging,
              bool deliberateWake) {
    if (deliberateWake || (mode == Mode::Legacy && legacyActivity != sleepActivity_)) forcedSleep_ = false;
    if (mode != Mode::Outside) temporaryWake_ = false;
    if (forcedSleep_) return false;
    if (mode == Mode::Inside) return true;
    if (mode == Mode::Legacy)
      return charging || timeoutMin == 0 || now - legacyActivity < timeoutMin * 60000UL;

    if (deliberateWake) {
      temporaryWake_ = true;
      wakeAt_ = now;
    }
    // Outside the window even "always on" has a finite wake timeout: one minute if unset, at most five.
    const uint8_t briefMinutes = timeoutMin == 0 ? 1 : (timeoutMin > 5 ? 5 : timeoutMin);
    if (temporaryWake_ && now - wakeAt_ >= briefMinutes * 60000UL) temporaryWake_ = false;
    return temporaryWake_;
  }

 private:
  bool forcedSleep_ = false;
  bool temporaryWake_ = false;
  uint32_t sleepActivity_ = 0;
  uint32_t wakeAt_ = 0;
};
}  // namespace screen
