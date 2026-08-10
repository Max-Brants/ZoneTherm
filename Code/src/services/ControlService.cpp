#include "ControlService.h"

ControlService::ControlService(ZoneRegistry& zones, ValveBank& valves,
                               ConfigStore& config)
    : zones_(zones), valves_(valves), config_(config) {}

void ControlService::tick() {
    if (paused_) return;

    const AppConfig cfg = config_.get();
    const auto snapshots = zones_.snapshotAll();

    ValvePlan::Bindings bindings{};
    for (int i = 0; i < kNumZones; i++) {
        bindings[i].enabled = cfg.zones[i].enabled;
        bindings[i].valveMask = cfg.zones[i].valveMask;
    }

    const uint8_t open = ValvePlan::solve(snapshots, bindings, cfg.control.mode,
                                          cfg.control.hysteresisK(), demand_);
    openMask_.store(open, std::memory_order_relaxed);
    valves_.setMask(open);
}
