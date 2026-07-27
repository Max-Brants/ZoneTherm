#include "UpdateService.h"

#include <cstdio>
#include <cstdlib>
#include <strings.h>
#include <vector>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"

#include "../app/Version.h"
#include "../util/Reboot.h"
#include "../util/StringUtils.h"

namespace {

const char* TAG = "UpdateService";

// GitHub rejects requests without a User-Agent.
const char* kUserAgent = "OpenThermController/" FW_VERSION " (esp-idf)";

constexpr int kHttpTimeoutMs = 20000;
constexpr const char* kFirmwareAsset = "firmware.bin";
constexpr const char* kFilesystemAsset = "littlefs.bin";

// A pending-verify image gets this long to prove it can reach the network
// before we deliberately reboot into the bootloader's rollback.
constexpr uint32_t kRollbackProbationS = 300;
// ...and this long of healthy uptime before we cancel the rollback.
constexpr uint32_t kHealthyUptimeS = 60;

uint32_t uptimeSeconds() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000);
}

// "v0.4.1" / "0.4.1" -> {0, 4, 1}. Missing or non-numeric components read
// as 0, so a malformed tag simply never compares as newer.
void parseVersion(const std::string& raw, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    size_t pos = (!raw.empty() && (raw[0] == 'v' || raw[0] == 'V')) ? 1 : 0;
    for (int i = 0; i < 3 && pos <= raw.size(); i++) {
        const size_t dot = raw.find('.', pos);
        const std::string part = raw.substr(pos, dot == std::string::npos
                                                     ? std::string::npos
                                                     : dot - pos);
        out[i] = StringUtils::toInt(part);
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
}

bool isNewer(const std::string& candidate, const std::string& installed) {
    int a[3], b[3];
    parseVersion(candidate, a);
    parseVersion(installed, b);
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

bool repoLooksValid(const std::string& repo) {
    const size_t slash = repo.find('/');
    return slash != std::string::npos && slash > 0 && slash + 1 < repo.size() &&
           repo.find(' ') == std::string::npos;
}

std::string assetUrl(const std::string& repo, const std::string& tag,
                     const char* asset) {
    return "https://github.com/" + repo + "/releases/download/" + tag + "/" + asset;
}

// esp_http_client_get_header() reads the *request* headers; the only way at a
// response header is this event, dispatched once per complete header line with
// user_data pointing at the std::string to fill.
esp_err_t captureLocation(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->user_data &&
        evt->header_key && evt->header_value &&
        strcasecmp(evt->header_key, "Location") == 0) {
        *static_cast<std::string*>(evt->user_data) = evt->header_value;
    }
    return ESP_OK;
}

}  // namespace

void UpdateService::begin() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.installedVersion = FW_VERSION;
        status_.state = UpdateState::Idle;
    }

    // With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE a freshly flashed image boots
    // in PENDING_VERIFY: unless it calls esp_ota_mark_app_valid_cancel_rollback()
    // the bootloader reverts to the previous slot on the next reset. tick()
    // makes that call once the device has proven it is healthy.
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &otaState) == ESP_OK &&
        otaState == ESP_OTA_IMG_PENDING_VERIFY) {
        rollbackResolved_ = false;
        pendingVerifyStartS_ = uptimeSeconds();
        std::lock_guard<std::mutex> lock(mutex_);
        status_.pendingVerify = true;
        ESP_LOGW(TAG, "Running a freshly flashed image on probation; rollback armed");
    }

    xTaskCreatePinnedToCore(
        [](void* self) { static_cast<UpdateService*>(self)->workerLoop(); },
        "update", 8192, this, tskIDLE_PRIORITY + 3, &task_, 0);
}

void UpdateService::tick(bool networkUp) {
    const uint32_t up = uptimeSeconds();

    if (!rollbackResolved_) {
        if (networkUp && up >= kHealthyUptimeS) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                ESP_LOGI(TAG, "New image confirmed healthy, rollback cancelled");
            }
            rollbackResolved_ = true;
            std::lock_guard<std::mutex> lock(mutex_);
            status_.pendingVerify = false;
        } else if (up - pendingVerifyStartS_ > kRollbackProbationS) {
            // Never got on the network: reboot and let the bootloader put the
            // previous, known-good image back.
            ESP_LOGE(TAG, "No network within probation window, rolling back");
            esp_restart();
        }
    }

    if (!networkUp || busy_) return;
    if (flashBusyProbe_ && flashBusyProbe_()) return;

    // tick() runs at 1 Hz but the schedule has hour granularity, and
    // ConfigStore::get() copies a dozen std::strings. Only look properly
    // twice a minute.
    if (up - lastScheduleLookS_ < 30 && lastScheduleLookS_ != 0) return;
    lastScheduleLookS_ = up;

    const AppConfig cfg = config_.get();
    if (!cfg.update.enabled || !repoLooksValid(cfg.update.repo)) return;

    // First check runs a minute after boot so it never competes with network
    // bring-up, MQTT connect and the initial OT poll round.
    const uint32_t intervalS =
        static_cast<uint32_t>(cfg.update.checkIntervalH) * 3600;
    const bool due = !firstCheckDone_ ? up >= kHealthyUptimeS
                                      : (up - lastCheckUptimeS_) >= intervalS;
    if (due) requestCheck();
}

bool UpdateService::requestCheck() {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return false;
    checkQueued_ = true;
    if (task_) xTaskNotifyGive(task_);
    return true;
}

bool UpdateService::requestInstall() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pendingTag_.empty()) return false;
    }
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return false;
    installQueued_ = true;
    if (task_) xTaskNotifyGive(task_);
    return true;
}

bool UpdateService::busy() const { return busy_; }

UpdateStatus UpdateService::status() const {
    // Read the config before taking mutex_: everything else in this class
    // acquires the two locks in that order, and mixing it here would be the
    // one path that could deadlock against a concurrent mutate().
    const bool autoInstall = config_.get().update.autoInstall;

    std::lock_guard<std::mutex> lock(mutex_);
    UpdateStatus copy = status_;
    copy.autoInstall = autoInstall;
    return copy;
}

void UpdateService::setState(UpdateState state, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = state;
    status_.message = message;
    if (state != UpdateState::Downloading) status_.progressPct = -1;
}

void UpdateService::setProgress(int pct) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.progressPct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

void UpdateService::workerLoop() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Drain everything queued before dropping busy_, so a request that
        // arrives while the previous one is finishing can never observe an
        // idle service and start a second flash.
        while (checkQueued_ || installQueued_) {
            if (checkQueued_) {
                checkQueued_ = false;
                doCheck();

                // Auto-install chains straight into the flash when the check
                // found something newer.
                bool proceed = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    proceed = status_.updateAvailable && !pendingTag_.empty();
                }
                if (proceed && config_.get().update.autoInstall) {
                    installQueued_ = true;
                }
            }

            if (installQueued_) {
                installQueued_ = false;
                doInstall();
            }
        }

        busy_ = false;
    }
}

void UpdateService::doCheck() {
    const AppConfig cfg = config_.get();
    if (!repoLooksValid(cfg.update.repo)) {
        setState(UpdateState::Failed, "Repository must be in owner/name form");
        return;
    }

    setState(UpdateState::Checking, "Contacting GitHub…");

    std::string tag;
    std::string error;
    if (!fetchLatestTag(cfg.update.repo, tag, error)) {
        ESP_LOGW(TAG, "Update check failed: %s", error.c_str());
        setState(UpdateState::Failed, error);
        return;
    }

    lastCheckUptimeS_ = uptimeSeconds();
    firstCheckDone_ = true;

    const bool newer = isNewer(tag, FW_VERSION);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.latestVersion = tag;
        status_.releaseUrl =
            "https://github.com/" + cfg.update.repo + "/releases/tag/" + tag;
        status_.lastCheckUptimeS = lastCheckUptimeS_;
        status_.updateAvailable = newer;
        pendingTag_ = newer ? tag : "";
    }

    if (newer) {
        ESP_LOGI(TAG, "Update available: %s (running %s)", tag.c_str(), FW_VERSION);
        setState(UpdateState::Available, "Version " + tag + " is available");
    } else {
        ESP_LOGI(TAG, "Up to date (latest %s, running %s)", tag.c_str(), FW_VERSION);
        setState(UpdateState::Idle, "Up to date (latest " + tag + ")");
    }
}

void UpdateService::doInstall() {
    const AppConfig cfg = config_.get();
    std::string tag;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tag = pendingTag_;
    }
    if (tag.empty()) {
        setState(UpdateState::Failed, "No release staged for install");
        return;
    }
    if (flashBusyProbe_ && flashBusyProbe_()) {
        setState(UpdateState::Available, "Deferred: a manual upload is in progress");
        return;
    }

    ESP_LOGW(TAG, "Installing release %s from %s", tag.c_str(), cfg.update.repo.c_str());
    setState(UpdateState::Downloading, "Downloading firmware…");
    setProgress(0);

    // Flash writes stall the CPU cache; keep OT bit-banging and I2C valve
    // traffic quiet for the duration, exactly as the manual upload path does.
    ot_.setPaused(true);
    control_.setPaused(true);

    std::string error;
    // Firmware first: it lands in the inactive OTA slot and is verified before
    // anything destructive happens. The filesystem write below has to erase the
    // live partition, so it runs only once the risky download has succeeded.
    if (!installFirmware(assetUrl(cfg.update.repo, tag, kFirmwareAsset), error)) {
        ESP_LOGE(TAG, "Firmware update failed: %s", error.c_str());
        ot_.setPaused(false);
        control_.setPaused(false);
        setState(UpdateState::Failed, "Firmware: " + error);
        return;
    }

    if (cfg.update.includeFilesystem) {
        setState(UpdateState::Downloading, "Downloading web UI…");
        setProgress(0);
        std::string fsError;
        if (!installFilesystem(assetUrl(cfg.update.repo, tag, kFilesystemAsset),
                               fsError)) {
            // Non-fatal: the new firmware is already staged and serves the REST
            // and OTA endpoints from flash, so a missing/half-written web UI is
            // recoverable with a manual filesystem upload after the reboot.
            ESP_LOGE(TAG, "Filesystem update failed (continuing): %s", fsError.c_str());
        }
    }

    ESP_LOGW(TAG, "Update to %s complete, rebooting", tag.c_str());
    setState(UpdateState::Installed, "Installed " + tag + ", rebooting…");
    scheduleReboot(2000);
}

bool UpdateService::fetchLatestTag(const std::string& repo, std::string& tagOut,
                                   std::string& errorOut) {
    const std::string url = "https://github.com/" + repo + "/releases/latest";
    std::string location;

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = kHttpTimeoutMs;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.user_agent = kUserAgent;
    cfg.event_handler = captureLocation;
    cfg.user_data = &location;
    cfg.disable_auto_redirect = true;  // the redirect *is* the answer

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        errorOut = "HTTP client init failed";
        return false;
    }

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        errorOut = std::string("connect failed: ") + esp_err_to_name(err);
    } else if (esp_http_client_fetch_headers(client) < 0) {
        errorOut = "no response headers";
    } else {
        const int status = esp_http_client_get_status_code(client);

        if (status != 301 && status != 302 && status != 307 && status != 308) {
            errorOut = "unexpected HTTP " + std::to_string(status) +
                       " (check the repository name)";
        } else if (location.empty()) {
            errorOut = "redirect without a Location header";
        } else {
            // .../releases/tag/v1.2.3 on success; a repo with no releases at all
            // redirects to .../releases instead.
            const std::string& loc = location;
            const std::string marker = "/releases/tag/";
            const size_t at = loc.find(marker);
            if (at == std::string::npos) {
                errorOut = "repository has no published releases";
            } else {
                tagOut = loc.substr(at + marker.size());
                while (!tagOut.empty() &&
                       (tagOut.back() == '/' || tagOut.back() == '\r' ||
                        tagOut.back() == '\n')) {
                    tagOut.pop_back();
                }
                ok = !tagOut.empty();
                if (!ok) errorOut = "empty release tag";
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

bool UpdateService::installFirmware(const std::string& url, std::string& errorOut) {
    esp_http_client_config_t http = {};
    http.url = url.c_str();
    http.timeout_ms = kHttpTimeoutMs;
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.user_agent = kUserAgent;
    http.keep_alive_enable = true;

    esp_https_ota_config_t otaCfg = {};
    otaCfg.http_config = &http;

    esp_https_ota_handle_t handle = nullptr;
    esp_err_t err = esp_https_ota_begin(&otaCfg, &handle);
    if (err != ESP_OK || !handle) {
        errorOut = std::string("download failed: ") + esp_err_to_name(err);
        return false;
    }

    const int imageSize = esp_https_ota_get_image_size(handle);
    for (;;) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        if (imageSize > 0) {
            setProgress(static_cast<int>(
                static_cast<int64_t>(esp_https_ota_get_image_len_read(handle)) *
                100 / imageSize));
        }
    }

    if (err != ESP_OK) {
        errorOut = esp_err_to_name(err);
        esp_https_ota_abort(handle);
        return false;
    }
    if (!esp_https_ota_is_complete_data_received(handle)) {
        errorOut = "connection closed before the whole image arrived";
        esp_https_ota_abort(handle);
        return false;
    }

    // Validates the image and points the bootloader at the new slot.
    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        errorOut = std::string("image rejected: ") + esp_err_to_name(err);
        return false;
    }

    setProgress(100);
    ESP_LOGI(TAG, "Firmware image written (%d bytes)", imageSize);
    return true;
}

bool UpdateService::installFilesystem(const std::string& url, std::string& errorOut) {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "littlefs");
    if (!part) {
        errorOut = "littlefs partition not found";
        return false;
    }

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = kHttpTimeoutMs;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.user_agent = kUserAgent;
    cfg.keep_alive_enable = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        errorOut = "HTTP client init failed";
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        errorOut = std::string("connect failed: ") + esp_err_to_name(err);
        esp_http_client_cleanup(client);
        return false;
    }

    const int64_t contentLen = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        // 404 simply means this release ships firmware only.
        errorOut = "HTTP " + std::to_string(status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    if (contentLen <= 0 || static_cast<size_t>(contentLen) > part->size) {
        errorOut = "image size " + std::to_string(contentLen) +
                   " does not fit the partition";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Past this point the live web UI is gone until the write completes. The
    // release asset is the mklittlefs image with its trailing erased (0xFF)
    // blocks stripped, so erasing the whole partition and writing only the
    // prefix reproduces the full image byte for byte.
    esp_vfs_littlefs_unregister("littlefs");
    err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        errorOut = std::string("erase failed: ") + esp_err_to_name(err);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    std::vector<char> buf(4096);
    size_t written = 0;
    bool ok = true;
    while (written < static_cast<size_t>(contentLen)) {
        const int read = esp_http_client_read(client, buf.data(), buf.size());
        if (read <= 0) {
            errorOut = "connection error after " + std::to_string(written) + " bytes";
            ok = false;
            break;
        }
        err = esp_partition_write(part, written, buf.data(), read);
        if (err != ESP_OK) {
            errorOut = std::string("flash write failed: ") + esp_err_to_name(err);
            ok = false;
            break;
        }
        written += read;
        setProgress(static_cast<int>(static_cast<int64_t>(written) * 100 / contentLen));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (ok) {
        ESP_LOGI(TAG, "Filesystem image written (%u bytes)",
                 static_cast<unsigned>(written));
    }
    return ok;
}
