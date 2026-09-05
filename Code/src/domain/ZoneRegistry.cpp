#include "ZoneRegistry.h"

ZoneSnapshot ZoneRegistry::snapshot(int zone) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return zones_[zone];
}

std::array<ZoneSnapshot, kNumZones> ZoneRegistry::snapshotAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return zones_;
}

void ZoneRegistry::noteRequest(int zone, uint32_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    zones_[zone].totalRequests++;
    zones_[zone].lastRequestMs = nowMs;
}

void ZoneRegistry::noteFailedRequest(int zone) {
    std::lock_guard<std::mutex> lock(mutex_);
    zones_[zone].failedRequests++;
}

void ZoneRegistry::applyDelta(int zone, const OtResponder::Delta& delta) {
    if (delta.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    ZoneState& z = zones_[zone];
    if (delta.setRoomTemp) z.roomTemp = delta.roomTemp;
    if (delta.setCoolingControl) z.coolingControl = delta.coolingControl;
    if (delta.setBoilerTemp) z.boilerTemp = delta.boilerTemp;
    if (delta.setModulation) z.modulation = delta.modulation;
    if (delta.clearOverride) z.overrideSetpoint = 0;
    if (delta.setSetpoint && z.setpoint != delta.setpoint) {
        // The dial on the room thermostat was turned - publish promptly.
        z.setpoint = delta.setpoint;
        dirtyMask_ |= 1u << zone;
    }
}

void ZoneRegistry::setOverrideSetpoint(int zone, float temperature) {
    if (temperature < 5.0f || temperature > 30.0f) return;
    std::lock_guard<std::mutex> lock(mutex_);
    zones_[zone].overrideSetpoint = temperature;
    dirtyMask_ |= 1u << zone;
}

void ZoneRegistry::resetDiagnostics(int zone) {
    std::lock_guard<std::mutex> lock(mutex_);
    ZoneState& z = zones_[zone];
    z.totalRequests = 0;
    z.failedRequests = 0;
    z.fault = false;
    z.errorCode = 0;
}

void ZoneRegistry::markDirty(int zone) {
    std::lock_guard<std::mutex> lock(mutex_);
    dirtyMask_ |= 1u << zone;
}

uint8_t ZoneRegistry::consumeDirtyMask() {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint8_t mask = dirtyMask_;
    dirtyMask_ = 0;
    return mask;
}
