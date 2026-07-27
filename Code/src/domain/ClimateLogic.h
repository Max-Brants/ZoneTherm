// The single valve-decision function. Every consumer (control loop, REST,
// MQTT "action" reporting) goes through here - the old firmware had three
// diverging copies of this comparison.
//
// No IDF dependencies - host-compilable.

#pragma once

#include "ZoneState.h"

namespace ClimateLogic {

// Bang-bang with a symmetric hysteresis band around the setpoint:
// heating opens below setpoint - h/2, closes above setpoint + h/2, holds
// in between; cooling is mirrored. A zone that is disabled, or whose
// thermostat has never reported a room temperature, keeps its valve closed
// (the old firmware opened all valves until the first report came in).
bool valveWanted(const ZoneSnapshot& zone, bool zoneEnabled, Season mode,
                 float hysteresisK, bool currentlyOpen);

// HA-style climate action string: "off", "idle", "heating" or "cooling".
const char* actionString(bool zoneEnabled, Season mode, bool valveOpen);

}  // namespace ClimateLogic
