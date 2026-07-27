// Home Assistant MQTT discovery payloads, built with cJSON so zone names
// with quotes/backslashes can no longer break the config JSON (they did in
// the old string-concatenation version).
//
#pragma once

#include <functional>
#include <string>

#include "ConfigStore.h"

namespace HaDiscovery {

using Publisher = std::function<bool(const std::string& topic,
                                     const std::string& payload, bool retained)>;

// One climate + three sensors per zone. The climate modes list follows the
// season ([off, heat] vs [off, cool]) - call again when the season flips.
void publishAll(const AppConfig& cfg, const Publisher& publish);

}  // namespace HaDiscovery
