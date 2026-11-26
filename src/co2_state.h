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
    // Die offiziellen Releases 0.316.0–0.317.3 haben noch keinen Ladepunkt-Optimierer.
    const char* version = root["ver"] | "";
    constexpr const char* legacy[] = {"0.316.0", "0.316.1", "0.316.2", "0.317.0", "0.317.1", "0.317.2", "0.317.3"};
    bool known = false;
    for (const char* release : legacy) known |= !strcmp(version, release);
    if (!known) return {};
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
