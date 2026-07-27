// The one JSON shape for a zone, shared by the REST API (GET
// /api/thermostats), the MQTT state topic and HA attributes - the old
// firmware hand-built three diverging copies of this.

#pragma once

#include "cJSON.h"

#include "../domain/ZoneState.h"
#include "ConfigStore.h"

// zoneIndex is 0-based; the JSON id field is 1-based like the old API/MQTT.
cJSON* zoneToJson(const ZoneSnapshot& zone, const ZoneConfig& zoneCfg,
                  int zoneIndex, Season season, bool valveOpen, uint32_t nowMs);

// Rounds to one decimal so floats don't serialize as 21.70000076293945.
void jsonAddRounded(cJSON* obj, const char* key, float value);
