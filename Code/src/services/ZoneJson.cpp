#include "ZoneJson.h"

#include <cmath>

#include "../domain/ClimateLogic.h"

namespace {

// A zone counts as active while its thermostat keeps polling; OT masters
// poll about once a second, so a minute of silence means unplugged/dead.
constexpr uint32_t kActiveWindowMs = 60 * 1000;

}  // namespace

void jsonAddRounded(cJSON* obj, const char* key, float value) {
    cJSON_AddNumberToObject(obj, key, std::round(value * 10.0) / 10.0);
}

void jsonAddValveArray(cJSON* obj, const char* key, uint8_t valveMask) {
    cJSON* list = cJSON_AddArrayToObject(obj, key);
    if (!list) return;
    for (int v = 0; v < kNumValves; v++) {
        if (valveMask & (1u << v)) {
            cJSON_AddItemToArray(list, cJSON_CreateNumber(v + 1));
        }
    }
}

cJSON* zoneToJson(const ZoneSnapshot& zone, const ZoneConfig& zoneCfg,
                  int zoneIndex, Season season, bool valveOpen, uint32_t nowMs) {
    cJSON* obj = cJSON_CreateObject();
    if (!obj) return nullptr;

    const bool active = zone.lastRequestMs != 0 &&
                        (nowMs - zone.lastRequestMs) < kActiveWindowMs;

    cJSON_AddNumberToObject(obj, "id", zoneIndex + 1);
    cJSON_AddStringToObject(obj, "name", zoneCfg.name.c_str());
    cJSON_AddBoolToObject(obj, "enabled", zoneCfg.enabled);
    jsonAddRounded(obj, "currentTemp", zone.roomTemp);
    jsonAddRounded(obj, "setpoint", zone.setpoint);
    cJSON_AddStringToObject(obj, "action",
                            ClimateLogic::actionString(zoneCfg.enabled, season, valveOpen));
    cJSON_AddStringToObject(obj, "status", active ? "Active" : "Inactive");
    cJSON_AddBoolToObject(obj, "valveOpen", valveOpen);
    jsonAddValveArray(obj, "valves", zoneCfg.valveMask);
    cJSON_AddNumberToObject(obj, "totalRequests", zone.totalRequests);
    cJSON_AddNumberToObject(obj, "failedRequests", zone.failedRequests);
    cJSON_AddNumberToObject(obj, "errorCode", zone.errorCode);
    jsonAddRounded(obj, "outsideTemp", zone.outsideTemp);
    jsonAddRounded(obj, "modulation", zone.modulation);
    return obj;
}
