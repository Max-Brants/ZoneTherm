// Owns the seven OpenTherm channels and the dedicated task that services
// them. Replaces the old pattern of blocking the main loop 40 ms per
// response: requests are answered by deadline instead - the handler runs
// immediately (pure OtResponder + registry delta), the response frame is
// queued with dueAt = now + 40 ms, and the task transmits it when due.
// All sends live on this one task, so two channels can never corrupt each
// other's bit timing (the property the old single-loop design relied on).

#pragma once

#include <atomic>
#include <cstdint>

#include "../domain/ZoneRegistry.h"
#include "../drivers/OtChannel.h"
#include "ConfigStore.h"

class OtEngine {
public:
    OtEngine(ZoneRegistry& zones, ConfigStore& config);

    // Spawns the OT task (pinned to core 1, above WiFi/lwIP priorities so
    // nothing preempts the 500 us half-bit bit-banging mid-frame).
    void start();

    // Pause protocol I/O (used while OTA writes flash). Thermostats retry,
    // so a paused span reads as a temporarily silent boiler.
    void setPaused(bool paused) { paused_ = paused; }

private:
    static void taskTrampoline(void* arg);
    [[noreturn]] void run();
    void onRequest(int zone, uint32_t request, bool valid);
    void refreshZoneContexts();

    ZoneRegistry& zones_;
    ConfigStore& config_;
    OtChannel channels_[kNumZones];

    struct Pending {
        uint32_t frame = 0;
        int64_t dueAtUs = 0;
        bool active = false;
    };
    Pending pending_[kNumZones];

    // Config snapshot refreshed ~1x/s on the OT task, so the per-request
    // path never touches the config lock.
    OtResponder::Context zoneCtx_[kNumZones];
    int64_t lastCtxRefreshUs_ = 0;

    std::atomic<bool> paused_{false};
};
