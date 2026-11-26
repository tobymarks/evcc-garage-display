// Host test: c++ -std=c++11 -Isrc -I/path/to/ArduinoJson/src tests/co2_limit.cpp -o /path/to/co2-test
#include <cassert>
#include <cstring>
#include "co2_state.h"

static co2::Limit read(const char* json) {
  JsonDocument doc;
  assert(!deserializeJson(doc, json));
  return co2::parse(doc.as<JsonObjectConst>(), doc["lp"].as<JsonObjectConst>());
}

int main() {
  auto unset = read(R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":null}})");
  assert(unset.supported && unset.value == co2::UNSET);
  auto zero = read(R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":0}})");
  assert(zero.supported && zero.value == 0);
  auto legacy = read(R"({"ver":"0.316.0","smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","smartCostLimit":159.5}})");
  assert(legacy.supported && legacy.value == 159.5);
  auto legacyPatch = read(R"({"ver":"0.317.3","smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","smartCostLimit":200}})");
  assert(legacyPatch.supported && legacyPatch.value == 200);

  const char* rejected[] = {
    R"({"smartCostType":"priceforecast","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":0.1}})",
    R"({"smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":null}})",
    R"({"smartCostType":"CO2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":null}})",
    R"({"smartCostType":"co2","smartCostAvailable":false,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":null}})",
    R"({"smartCostType":"co2","smartCostAvailable":"true","lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":null}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":"250"}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":true}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":-4}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":true,"smartCostLimit":null}})",
    R"({"ver":"0.319.1","smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","smartCostLimit":null}})",
    R"({"ver":"0.317.0","smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":null,"smartCostLimit":null}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"once","optimizerControlled":false,"smartCostLimit":null}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","optimizerControlled":false,"smartCostLimit":null}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"now","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":null}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"off","alwaysCharge":"off","optimizerControlled":false,"smartCostLimit":null}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"pv","optimizerControlled":false,"smartCostLimit":null}})",
    R"({"smartCostType":"co2","smartCostAvailable":true,"lp":{"mode":"smart","alwaysCharge":"off","optimizerControlled":false,"chargerFeatureHeating":true,"smartCostLimit":null}})",
  };
  for (auto json : rejected) assert(!read(json).supported);

  char path[32];
  assert(co2::prepareWrite(path, sizeof(path), unset, co2::UNSET, 0));
  assert(!std::strcmp(path, "/smartcostlimit/0"));
  assert(co2::prepareWrite(path, sizeof(path), zero, 0, 245));
  assert(!std::strcmp(path, "/smartcostlimit/245"));
  assert(!co2::prepareWrite(path, sizeof(path), zero, co2::UNSET, 245));
  assert(!co2::prepareWrite(path, sizeof(path), {}, 0, 245));
  assert(!co2::prepareWrite(path, sizeof(path), zero, 0, -1));
  assert(!co2::prepareWrite(path, sizeof(path), zero, 0, co2::MAX_VALUE + 1));
  assert(!co2::prepareWrite(path, sizeof(path), zero, 0, 0));
  assert(!co2::prepareWrite(path, 1, zero, 0, 245));
  assert(co2::confirmed(zero, 0));
  assert(!co2::confirmed(unset, 0));
  assert(!co2::confirmed(legacy, 160));
  assert(!co2::confirmed({}, 0));
}
