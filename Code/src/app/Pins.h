// Board wiring for the ZoneTherm PCB. GPIO/IO numbers, not the WROOM-1
// module's physical package pin numbers; must match the Thermos_In_X /
// Thermos_Out_X and SDA/SCL nets on the schematic.

#pragma once

#include <cstdint>

#include "../domain/ZoneState.h"

namespace pins {

constexpr uint8_t kThermIn[kNumZones] = {2, 42, 40, 48, 20, 18, 16};
constexpr uint8_t kThermOut[kNumZones] = {1, 41, 39, 47, 19, 17, 15};
constexpr uint8_t kI2cSda = 4;
constexpr uint8_t kI2cScl = 5;

}  // namespace pins
