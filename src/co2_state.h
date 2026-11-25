#pragma once

#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

#include "co2_limit.h"

namespace co2 {
inline Limit parse(JsonObjectConst root, JsonObjectConst lp) {
  if (strcmp(root["smartCostType"] | "", "co2") || !root["smartCostAvailable"].is<bool>() ||
      !root["smartCostAvailable"].as<bool>() || strcmp(lp["mode"] | "", "smart") ||
      strcmp(lp["alwaysCharge"] | "", "off") || (lp["chargerFeatureHeating"] | false))
    return {};

  const auto optimizer = lp["optimizerControlled"];
  if (!optimizer.isUnbound()) {
    if (!optimizer.is<bool>() || optimizer.as<bool>()) return {};
  } else {
    // Nur die geprüften älteren Versionen kennen dieses Feld noch nicht.
    const char* version = root["ver"] | "";
    if (strcmp(version, "0.316.0") && strcmp(version, "0.317.0")) return {};
  }

  const auto value = lp["smartCostLimit"];
  if (value.isUnbound()) return {};  // fehlt ≠ explizit null
  if (value.isNull()) return {true, UNSET};
  if (!value.is<double>()) return {};
  const double limit = value.as<double>();
  if (!std::isfinite(limit) || limit < 0 || limit > MAX_VALUE) return {};
  return {true, limit};
}
}  // namespace co2
