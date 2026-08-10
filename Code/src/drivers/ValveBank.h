// The seven valve outputs behind the MCP23017 I2C expander (port B).
// Replaces the old Valve + HardwareManager pair; the shadow latch makes
// isValveOpen() real instead of the old hardcoded-true stub.
//
// Valves are indexed independently of thermostats - which thermostat drives
// which valve is a config question, answered in ValvePlan/ControlService.

#pragma once

#include <cstdint>

#include "driver/i2c_master.h"

class MCP23017;

class ValveBank {
public:
    // Creates the I2C bus and probes for the expander; if it is absent,
    // valve control is disabled (setMask() becomes a no-op) but the rest of
    // the firmware keeps running.
    void begin(uint8_t sdaPin, uint8_t sclPin);

    // bit v set (V1 = bit 0) = valve v open. The whole bank moves in one I2C
    // write, and only when something actually changed - so the three loops of
    // a single room open in the same transaction instead of three, with no
    // window where half the set has moved.
    void setMask(uint8_t valveMask);

    bool isValveOpen(int valve) const;
    uint8_t mask() const;  // logical (unshifted) mask, for diagnostics
    bool present() const { return present_; }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    MCP23017* mcp_ = nullptr;
    bool present_ = false;
    uint8_t shadow_ = 0;  // last value written to port B
};
