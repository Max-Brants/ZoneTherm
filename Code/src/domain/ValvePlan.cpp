#include "ValvePlan.h"

namespace ValvePlan {

uint8_t solve(const Snapshots& zones, const Bindings& bindings, Season mode,
              float hysteresisK, Demand& demand) {
    uint8_t open = 0;
    for (int i = 0; i < kNumZones; i++) {
        demand[i] = ClimateLogic::valveWanted(zones[i], bindings[i].enabled, mode,
                                              hysteresisK, demand[i]);
        if (demand[i]) open |= bindings[i].valveMask;
    }
    return static_cast<uint8_t>(open & kAllValvesMask);
}

bool exclusive(const Masks& masks) {
    uint8_t seen = 0;
    for (uint8_t m : masks) {
        if (m & seen) return false;
        seen = static_cast<uint8_t>(seen | m);
    }
    return true;
}

bool dropOverlaps(Masks& masks) {
    uint8_t seen = 0;
    bool changed = false;
    for (uint8_t& m : masks) {
        const uint8_t kept = static_cast<uint8_t>(m & ~seen & kAllValvesMask);
        if (kept != m) changed = true;
        m = kept;
        seen = static_cast<uint8_t>(seen | kept);
    }
    return changed;
}

uint8_t unassigned(const Masks& masks) {
    uint8_t seen = 0;
    for (uint8_t m : masks) seen = static_cast<uint8_t>(seen | m);
    return static_cast<uint8_t>(~seen & kAllValvesMask);
}

}  // namespace ValvePlan
