// Unattended firmware/filesystem updates pulled from GitHub Releases.
//
// Version discovery deliberately avoids the GitHub REST API. This board has
// no PSRAM, and a /releases/latest JSON body (release notes plus an entry per
// asset) would have to be buffered and handed to cJSON on a heap already
// shared with httpd, MQTT and the OT engine - tens of KB for three fields we
// actually want. Instead the updater requests
// https://github.com/<repo>/releases/latest with redirects DISABLED and reads
// the tag out of the 302's Location header: one request, zero body bytes, no
// API rate limit, and it works for hand-made releases too. Assets then live
// at predictable .../releases/download/<tag>/<name> URLs.
//
// All network work happens on a dedicated task; the HTTP handlers and the
// main control loop only ever post a request and read a status snapshot.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ConfigStore.h"
#include "ControlService.h"
#include "OtEngine.h"

enum class UpdateState {
    Idle,         // nothing in flight
    Checking,     // asking GitHub for the latest tag
    Available,    // a newer release exists, waiting for install (notify mode)
    Downloading,  // flashing firmware and/or filesystem
    Failed,       // last operation failed, see message
    Installed,    // flashed, reboot scheduled
};

struct UpdateStatus {
    UpdateState state = UpdateState::Idle;
    std::string installedVersion;
    std::string latestVersion;  // empty until a check has succeeded
    std::string releaseUrl;     // GitHub page for latestVersion
    std::string message;        // last error, or human-readable progress
    int progressPct = -1;       // 0..100 while downloading, -1 otherwise
    uint32_t lastCheckUptimeS = 0;  // 0 = never checked this boot
    bool updateAvailable = false;
    bool autoInstall = false;
    bool pendingVerify = false;  // running image is on trial, rollback armed
};

class UpdateService {
public:
    UpdateService(ConfigStore& config, OtEngine& ot, ControlService& control)
        : config_(config), ot_(ot), control_(control) {}

    // Reports whether something outside this service is writing flash - the
    // manual upload path in OtaRoutes. Injected from Main so services/ keeps
    // no dependency on web/. Never installs while this returns true.
    void setFlashBusyProbe(std::function<bool()> probe) {
        flashBusyProbe_ = std::move(probe);
    }

    // Starts the worker task and records whether the running image is a
    // freshly flashed one still on probation (see tick()).
    void begin();

    // Called at 1 Hz from the main loop. Drives the periodic check schedule
    // and resolves the rollback verdict for a pending-verify image.
    void tick(bool networkUp);

    // Both return false when the worker is already busy. Work is queued, not
    // performed inline - callers are HTTP handlers.
    bool requestCheck();
    bool requestInstall();

    bool busy() const;
    UpdateStatus status() const;

private:
    void workerLoop();
    void doCheck();
    void doInstall();

    // GET without redirect following; pulls the tag out of the Location header.
    bool fetchLatestTag(const std::string& repo, std::string& tagOut,
                        std::string& errorOut);
    bool installFirmware(const std::string& url, std::string& errorOut);
    bool installFilesystem(const std::string& url, std::string& errorOut);

    void setState(UpdateState state, const std::string& message = "");
    void setProgress(int pct);

    ConfigStore& config_;
    OtEngine& ot_;
    ControlService& control_;
    std::function<bool()> flashBusyProbe_;

    mutable std::mutex mutex_;
    UpdateStatus status_;
    std::string pendingTag_;  // tag discovered by the last successful check

    TaskHandle_t task_ = nullptr;
    // Claimed with a compare-exchange: tick() runs on the main task while
    // requestCheck/requestInstall arrive on an httpd worker, so a plain
    // test-then-set would let both through.
    std::atomic<bool> busy_{false};
    std::atomic<bool> checkQueued_{false};
    std::atomic<bool> installQueued_{false};

    uint32_t lastCheckUptimeS_ = 0;
    uint32_t lastScheduleLookS_ = 0;
    bool firstCheckDone_ = false;
    bool rollbackResolved_ = true;  // false only for a pending-verify image
    uint32_t pendingVerifyStartS_ = 0;
};
