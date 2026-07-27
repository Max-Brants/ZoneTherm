#include "ClimateLogic.h"

namespace ClimateLogic {

bool valveWanted(const ZoneSnapshot& zone, bool zoneEnabled, Season mode,
                 float hysteresisK, bool currentlyOpen) {
    if (!zoneEnabled) return false;
    if (zone.roomTemp <= 0) return false;  // no valid reading yet - fail safe

    const float half = hysteresisK / 2.0f;
    if (mode == Season::Heating) {
        if (zone.roomTemp < zone.setpoint - half) return true;
        if (zone.roomTemp > zone.setpoint + half) return false;
    } else {
        if (zone.roomTemp > zone.setpoint + half) return true;
        if (zone.roomTemp < zone.setpoint - half) return false;
    }
    return currentlyOpen;  // inside the band: hold
}

const char* actionString(bool zoneEnabled, Season mode, bool valveOpen) {
    if (!zoneEnabled) return "off";
    if (!valveOpen) return "idle";
    return mode == Season::Heating ? "heating" : "cooling";
}

}  // namespace ClimateLogic
