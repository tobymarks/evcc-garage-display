// Minimaler Ersatz für Arduino.h, damit ui.cpp im Simulator auf dem Rechner läuft
#pragma once
#include <math.h>
#include <stdint.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

uint32_t millis();
#ifndef constrain
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#endif
