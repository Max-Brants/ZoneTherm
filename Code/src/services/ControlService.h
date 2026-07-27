// The 1 Hz control tick: turns zone snapshots into valve positions through
// ClimateLogic (the single decision function) and drives the ValveBank.
// Runs on the main task; the I2C writes here must never happen under the
// zone lock, which snapshots guarantee.

#pragma once

#include <atomic>

#include "../domain/ZoneRegistry.h"
#include "../drivers/ValveBank.h"
#include "ConfigStore.h"

class ControlService {
public:
    ControlService(ZoneRegistry& zones, ValveBank& valves, ConfigStore& config);

    void tick();

    // Pause valve actuation (used while OTA writes flash).
    void setPaused(bool paused) { paused_ = paused; }

    bool valveOpen(int zone) const { return valves_.isOpen(zone); }

private:
    ZoneRegistry& zones_;
    ValveBank& valves_;
    ConfigStore& config_;
    std::atomic<bool> paused_{false};
};
