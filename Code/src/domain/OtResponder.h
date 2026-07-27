// Pure OpenTherm slave request handler: request frame + zone snapshot in,
// response frame + state delta out. No IDF dependencies - host-compilable.
//
// The caller (OtEngine) snapshots the zone, calls handle(), applies the
// returned delta back through ZoneRegistry and transmits the response.
// Keeping this side-effect free is what makes the protocol logic testable.

#pragma once

#include "OtFrame.h"
#include "ZoneState.h"

namespace OtResponder {

// How the zone participates in the plant right now (from config).
struct Context {
    bool zoneEnabled = true;
    Season mode = Season::Heating;
};

// Fields the request wants written back into the zone. Expressed as a
// sparse delta (not a full ZoneState copy) so concurrent command writes
// between snapshot and apply are never clobbered.
struct Delta {
    bool setRoomTemp = false;
    bool setSetpoint = false;
    bool setCoolingControl = false;
    bool setBoilerTemp = false;
    bool setOutsideTemp = false;
    bool setModulation = false;
    bool clearOverride = false;

    float roomTemp = 0;
    float setpoint = 0;
    float coolingControl = 0;
    float boilerTemp = 0;
    float outsideTemp = 0;
    float modulation = 0;

    bool empty() const {
        return !(setRoomTemp || setSetpoint || setCoolingControl ||
                 setBoilerTemp || setOutsideTemp ||
                 setModulation || clearOverride);
    }
};

struct Result {
    ot::Frame response = 0;
    bool hasResponse = false;
    Delta delta;
};

Result handle(ot::Frame request, const ZoneSnapshot& zone, const Context& ctx);

}  // namespace OtResponder
