#pragma once

#include <time.h>

namespace sun {
// Sonnenhöhe in Grad über dem Horizont (Näherung, genau auf ca. 0,5°)
double elevation(time_t utc, double latitude, double longitude);
}  // namespace sun
