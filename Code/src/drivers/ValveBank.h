// The seven zone valves behind the MCP23017 I2C expander (port B).
// Replaces the old Valve + HardwareManager pair; the shadow latch makes
// isOpen() real instead of the old hardcoded-true stub.

#pragma once

#include <cstdint>

#include "driver/i2c_master.h"

class MCP23017;

class ValveBank {
public:
    // Creates the I2C bus and probes for the expander; if it is absent,
    // valve control is disabled (set() becomes a no-op) but the rest of
    // the firmware keeps running.
    void begin(uint8_t sdaPin, uint8_t sclPin);

    void set(int zone, bool open);  // writes I2C only on actual change
    bool isOpen(int zone) const;
    bool present() const { return present_; }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    MCP23017* mcp_ = nullptr;
    bool present_ = false;
    uint8_t shadow_ = 0;  // last value written to port B
};
