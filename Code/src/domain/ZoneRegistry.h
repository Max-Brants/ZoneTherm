// Owns all zone state and the single lock that guards it. Callers get value
// copies (ZoneSnapshot); only the methods here take the lock, and nothing
// slow (I/O, JSON, NVS) ever runs under it - that discipline used to be a
// comment convention around a global recursive mutex, now it's structural.
//
// No IDF dependencies - host-compilable.

#pragma once

#include <array>
#include <mutex>

#include "OtResponder.h"
#include "ZoneState.h"

class ZoneRegistry {
public:
    ZoneSnapshot snapshot(int zone) const;
    std::array<ZoneSnapshot, kNumZones> snapshotAll() const;

    // OT engine path
    void noteRequest(int zone, uint32_t nowMs);
    void noteFailedRequest(int zone);
    void applyDelta(int zone, const OtResponder::Delta& delta);

    // Command path (MQTT / REST). Values outside 5..30 degC are ignored,
    // matching the old firmware's setpoint clamp.
    void setOverrideSetpoint(int zone, float temperature);
    void resetDiagnostics(int zone);

    // Dirty zones want a prompt MQTT publish instead of waiting for the
    // 30 s cadence. Returns a bitmask (bit i = zone i) and clears it.
    void markDirty(int zone);
    uint8_t consumeDirtyMask();

private:
    mutable std::mutex mutex_;
    std::array<ZoneState, kNumZones> zones_{};
    uint8_t dirtyMask_ = 0;
};
