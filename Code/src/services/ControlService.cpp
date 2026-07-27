#include "ControlService.h"

#include "../domain/ClimateLogic.h"

ControlService::ControlService(ZoneRegistry& zones, ValveBank& valves,
                               ConfigStore& config)
    : zones_(zones), valves_(valves), config_(config) {}

void ControlService::tick() {
    if (paused_) return;

    const AppConfig cfg = config_.get();
    const auto snapshots = zones_.snapshotAll();

    for (int i = 0; i < kNumZones; i++) {
        const bool wanted = ClimateLogic::valveWanted(
            snapshots[i], cfg.zones[i].enabled, cfg.control.mode,
            cfg.control.hysteresisK(), valves_.isOpen(i));
        valves_.set(i, wanted);
    }
}
