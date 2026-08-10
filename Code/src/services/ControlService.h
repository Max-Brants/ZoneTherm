// The 1 Hz control tick: turns zone snapshots into valve positions through
// ClimateLogic (the per-room decision) and ValvePlan (which valves that adds
// up to), then drives the ValveBank. Runs on the main task; the I2C writes
// here must never happen under the zone lock, which snapshots guarantee.

#pragma once

#include <atomic>

#include "../domain/ValvePlan.h"
#include "../domain/ZoneRegistry.h"
#include "../drivers/ValveBank.h"
#include "ConfigStore.h"

class ControlService {
public:
    ControlService(ZoneRegistry& zones, ValveBank& valves, ConfigStore& config);

    void tick();

    // Pause valve actuation (used while OTA writes flash).
    void setPaused(bool paused) { paused_ = paused; }

    // The control loop's intent, which is also what went to the expander
    // unless it is missing (see ValveBank::present()). Read from the HTTP and
    // MQTT tasks, written by the main task - hence the atomic. Callers pair it
    // with their zone's valveMask through ValvePlan::anyOpen(); there is
    // deliberately no per-zone helper here, because that would copy the whole
    // AppConfig once per zone.
    uint8_t openValveMask() const { return openMask_.load(std::memory_order_relaxed); }

private:
    ZoneRegistry& zones_;
    ValveBank& valves_;
    ConfigStore& config_;
    std::atomic<bool> paused_{false};

    // The hysteresis hold used to be read back off the expander shadow, which
    // only worked while every zone owned exactly one valve. A zone can now own
    // several or none, so the previous answer is latched here instead.
    // Main task only.
    ValvePlan::Demand demand_{};
    std::atomic<uint8_t> openMask_{0};
};
