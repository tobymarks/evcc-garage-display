#include "sun.h"

#include <math.h>

static constexpr double DEG = M_PI / 180.0;

double sun::elevation(time_t utc, double latitude, double longitude) {
  const double n = utc / 86400.0 + 2440587.5 - 2451545.0;  // Tage seit J2000
  const double L = fmod(280.460 + 0.9856474 * n, 360.0);
  const double g = fmod(357.528 + 0.9856003 * n, 360.0) * DEG;
  const double lambda = (L + 1.915 * sin(g) + 0.020 * sin(2 * g)) * DEG;
  const double eps = (23.439 - 0.0000004 * n) * DEG;

  const double ra = atan2(cos(eps) * sin(lambda), cos(lambda));
  const double dec = asin(sin(eps) * sin(lambda));
  const double gmst = fmod(280.46061837 + 360.98564736629 * n, 360.0);
  const double ha = (gmst + longitude) * DEG - ra;

  const double lat = latitude * DEG;
  return asin(sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(ha)) / DEG;
}
