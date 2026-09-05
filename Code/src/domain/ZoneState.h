// Plain zone state shared between the OT engine, control loop, MQTT and web.
// No IDF/FreeRTOS dependencies - host-compilable.

#pragma once

#include <cstdint>

constexpr int kNumZones = 7;   // thermostat channels (OpenTherm slave ports)
constexpr int kNumValves = 7;  // valve outputs V1..V7 on the MCP23017 port B

// A thermostat drives a set of valves, carried as a bitmask with V1 = bit 0.
// The two counts are independent on purpose: they are equal on this board, but
// nothing below couples a thermostat index to a valve index any more.
static_assert(kNumValves <= 8, "valve sets are carried in a uint8_t");
constexpr uint8_t kAllValvesMask = static_cast<uint8_t>((1u << kNumValves) - 1);

// Global season: one heating/cooling plant, all zones follow it.
enum class Season : uint8_t {
    Heating = 0,
    Cooling = 1,
};

struct ZoneState {
    // Climate values captured from / pushed to the room thermostat
    float setpoint = 20.0f;         // last room setpoint the thermostat sent (TrSet)
    float overrideSetpoint = 0.0f;  // pending TrOverride push; 0 = no override
    float roomTemp = 0.0f;          // 0 = thermostat has not reported yet
    float boilerTemp = 0.0f;
    float modulation = 0.0f;
    float coolingControl = 0.0f;

    bool flameOn = false;
    bool fault = false;
    uint8_t errorCode = 0;

    // Diagnostics
    uint32_t totalRequests = 0;
    uint32_t failedRequests = 0;
    uint32_t lastRequestMs = 0;  // 0 = never seen a request
};

// Value copy handed out by ZoneRegistry; safe to read without holding any lock.
using ZoneSnapshot = ZoneState;
