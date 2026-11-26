// Host test: c++ -std=c++11 -Isrc tests/screen_schedule.cpp -o /tmp/screen-schedule && /tmp/screen-schedule
#include <cassert>
#include <cstdlib>
#include <ctime>
#include "screen_schedule.h"
#include "config.h"

static void windowBoundaries() {
  const screen::Window day{true, 8 * 60, 22 * 60};
  assert(day.mode(8 * 60) == screen::Mode::Inside);
  assert(day.mode(22 * 60 - 1) == screen::Mode::Inside);
  assert(day.mode(8 * 60 - 1) == screen::Mode::Outside);
  assert(day.mode(22 * 60) == screen::Mode::Outside);
  const screen::Window night{true, 22 * 60, 8 * 60};
  assert(night.mode(22 * 60) == screen::Mode::Inside);
  assert(night.mode(0) == screen::Mode::Inside);
  assert(night.mode(8 * 60) == screen::Mode::Outside);
  assert(day.mode(-1) == screen::Mode::Legacy);
  assert(screen::Window{}.mode(8 * 60) == screen::Mode::Legacy);
  const screen::Window equal{true, 480, 480};
  assert(equal.mode(480) == screen::Mode::Legacy);
}

using screen::Mode;

static void scheduledPower() {
  screen::Power p;
  assert(p.keepOn(Mode::Inside, 0, 0, 0, false, false));
  assert(!p.keepOn(Mode::Outside, 1, 1, 0, true, false));
  assert(!p.keepOn(Mode::Outside, 2, 2, 5, true, false));
  // An explicit wake with the former “always on” setting is finite.
  assert(p.keepOn(Mode::Outside, 3, 3, 0, true, true));
  assert(p.keepOn(Mode::Outside, 60002, 60002, 0, true, false));
  assert(!p.keepOn(Mode::Outside, 60003, 60003, 0, true, false));
  // Holding or tapping again outside the window starts the short timeout again.
  assert(p.keepOn(Mode::Outside, 100000, 100000, 240, false, true));
  assert(p.keepOn(Mode::Outside, 350000, 350000, 240, false, true));
  assert(!p.keepOn(Mode::Outside, 650000, 650000, 240, false, false));
  // An inside-window touch does not grant a wake interval after closing time.
  assert(p.keepOn(Mode::Inside, 700000, 700000, 0, false, true));
  assert(!p.keepOn(Mode::Outside, 700001, 700000, 0, true, false));
}

static void manualSleep() {
  screen::Power p;
  p.sleep(1);
  assert(p.isForcedAsleep());
  assert(!p.keepOn(Mode::Outside, 2, 2, 0, true, false));
  assert(!p.keepOn(Mode::Inside, 3, 3, 0, true, false));
  assert(p.keepOn(Mode::Inside, 3, 3, 0, true, true));
  assert(!p.isForcedAsleep());
  p.sleep(3);
  assert(p.keepOn(Mode::Outside, 4, 4, 2, true, true));
  assert(p.keepOn(Mode::Outside, 120003, 120003, 2, true, false));
  assert(!p.keepOn(Mode::Outside, 120004, 120004, 2, true, false));
  assert(p.keepOn(Mode::Outside, 120005, 120005, 2, true, true));
  p.sleep(120005);
  assert(!p.keepOn(Mode::Outside, 120006, 120006, 2, true, false));
  assert(!p.keepOn(Mode::Inside, 120007, 120007, 2, true, false));
  // Legacy mode still clears /sleep on loadpoint activity, not charging alone.
  p.sleep(120007);
  assert(!p.keepOn(Mode::Legacy, 120008, 120007, 0, true, false));
  assert(p.keepOn(Mode::Legacy, 120009, 120009, 0, true, false));
}

static void fallbackAndWrap() {
  screen::Power p;
  assert(p.keepOn(Mode::Legacy, 60000, 0, 0, false, false));
  assert(p.keepOn(Mode::Legacy, 60000, 0, 1, true, false));
  assert(!p.keepOn(Mode::Legacy, 60000, 0, 1, false, false));
  assert(p.keepOn(Mode::Legacy, 60000, 1, 1, false, false));
  assert(p.keepOn(Mode::Outside, UINT32_MAX - 1, 0, 0, true, true));
  assert(p.keepOn(Mode::Outside, 0, 0, 0, true, false));
  assert(!p.keepOn(Mode::Outside, 59998, 0, 0, true, false));
  assert(!p.keepOn(Mode::Outside, UINT32_MAX, 0, 0, true, false));
}

static void daylightSaving() {
  assert(setenv("TZ", TIMEZONE, 1) == 0);
  tzset();
  const auto utc = [](int month, int day, int hour, int min) {
    struct tm t{};
    t.tm_year = 126;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    return timegm(&t);
  };
  const screen::Window window{true, 2 * 60, 3 * 60};
  // CET → CEST skips the whole window; CEST → CET repeats it.
  assert(window.modeAt(utc(3, 29, 0, 59), true) == Mode::Outside);
  assert(window.modeAt(utc(3, 29, 1, 0), true) == Mode::Outside);
  assert(window.modeAt(utc(10, 25, 0, 30), true) == Mode::Inside);
  assert(window.modeAt(utc(10, 25, 1, 30), true) == Mode::Inside);
  assert(window.modeAt(utc(10, 25, 2, 0), true) == Mode::Outside);
  assert(window.modeAt(utc(10, 25, 1, 30), false) == Mode::Legacy);
}

static void settingsRoundTrip() {
  screen::Window window;
  assert(!screen::Window::fromStored(0).enabled);
  assert(window.fromForm("1", "22:30", "00:00"));
  const auto stored = window.store();
  const auto loaded = screen::Window::fromStored(stored);
  assert(loaded.enabled && loaded.start == 22 * 60 + 30 && loaded.end == 0);
  assert(!window.fromForm("1", "08:00", "08:00"));
  assert(!window.fromForm("1", "24:00", "00:00"));
  assert(!window.fromForm("1", "08:60", "00:00"));
  assert(!window.fromForm("1", "8:00", "00:00"));
  assert(!window.fromForm("1", "08:00x", "00:00"));
  assert(!window.fromForm("1", "08:00", ""));
  assert(!window.fromForm("", "08:00", "00:00"));
  assert(!window.fromForm("01", "08:00", "00:00"));
  assert(window.store() == stored);
  assert(!screen::Window::fromStored(UINT32_MAX).enabled);
  assert(window.fromForm("0", "22:30", "00:00"));
  const auto disabled = screen::Window::fromStored(window.store());
  assert(!disabled.enabled && disabled.start == loaded.start && disabled.end == loaded.end);
}

int main() {
  windowBoundaries();
  scheduledPower();
  manualSleep();
  fallbackAndWrap();
  daylightSaving();
  settingsRoundTrip();
}
