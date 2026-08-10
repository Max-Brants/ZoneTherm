// The one JSON shape for a zone, shared by the REST API (GET
// /api/thermostats), the MQTT state topic and HA attributes - the old
// firmware hand-built three diverging copies of this.

#pragma once

#include "cJSON.h"

#include "../domain/ZoneState.h"
#include "ConfigStore.h"

// zoneIndex is 0-based; the JSON id field is 1-based like the old API/MQTT.
// valveOpen means "at least one of this zone's valves is open", so it is false
// for a zone that owns none however loudly it is asking - pass
// ValvePlan::anyOpen(zoneCfg.valveMask, control.openValveMask()).
cJSON* zoneToJson(const ZoneSnapshot& zone, const ZoneConfig& zoneCfg,
                  int zoneIndex, Season season, bool valveOpen, uint32_t nowMs);

// Rounds to one decimal so floats don't serialize as 21.70000076293945.
void jsonAddRounded(cJSON* obj, const char* key, float value);

// Emits a valve set as a 1-based number array ("valves":[1,3,4]) - the same
// shape GET/POST /api/config uses, so the wire format is readable in a curl.
void jsonAddValveArray(cJSON* obj, const char* key, uint8_t valveMask);
