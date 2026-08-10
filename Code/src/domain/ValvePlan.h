// Which valves should be open right now. ClimateLogic answers that for one
// thermostat; this answers it for the whole manifold, where a thermostat can
// drive several valves (a room with three underfloor loops) or none at all.
//
// No IDF dependencies - host-compilable.

#pragma once

#include <array>
#include <cstdint>

#include "ClimateLogic.h"
#include "ZoneState.h"

namespace ValvePlan {

struct ZoneBinding {
    bool enabled = false;
    uint8_t valveMask = 0;  // bit v set = this thermostat drives valve v
};

using Snapshots = std::array<ZoneSnapshot, kNumZones>;
using Bindings = std::array<ZoneBinding, kNumZones>;
using Masks = std::array<uint8_t, kNumZones>;
using Demand = std::array<bool, kNumZones>;

// Runs ClimateLogic::valveWanted for every zone against `demand` - the caller's
// latch of the previous answer, updated in place - and returns the union of the
// valve masks of the zones that are asking.
//
// The latch replaces the old read-back of the expander shadow, which could only
// ever work while the mapping was 1:1: it cannot express "this zone owns three
// valves" and it has no bit at all to read for a zone that owns none.
// Overlapping masks are harmless here - a valve opens if any owner is asking -
// so nothing in the control path depends on exclusivity being enforced.
uint8_t solve(const Snapshots& zones, const Bindings& bindings, Season mode,
              float hysteresisK, Demand& demand);

// Is flow actually reaching this room? False for a zone that owns no valves,
// however loudly it is asking - which is what makes the reported action honest.
inline bool anyOpen(uint8_t zoneMask, uint8_t openMask) {
    return (zoneMask & openMask) != 0;
}

// True when no valve is claimed twice. The API layer rejects a config that
// fails this; the control loop neither needs nor checks it.
bool exclusive(const Masks& masks);

// Clears bits already claimed by a lower-index zone; returns true if it had to
// change anything, which the NVS load path turns into a warning.
bool dropOverlaps(Masks& masks);

// bit v set = valve v is claimed by nobody and will therefore never open.
uint8_t unassigned(const Masks& masks);

}  // namespace ValvePlan
