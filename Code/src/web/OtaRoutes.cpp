#include "OtaRoutes.h"

#include <vector>

#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"

#include "../util/Reboot.h"
#include "../util/StringUtils.h"
#include "HttpServer.h"

namespace {

const char* TAG = "OtaRoutes";

enum class OtaMode { Firmware, Filesystem };

struct OtaSession {
    bool active = false;
    bool isFilesystem = false;
    bool fsUnmounted = false;
    size_t receivedBytes = 0;
    int64_t startedAtUs = 0;

    esp_ota_handle_t otaHandle = 0;
    const esp_partition_t* otaPartition = nullptr;

    const esp_partition_t* fsPartition = nullptr;
    size_t fsWriteOffset = 0;
};

OtaSession session;
WebContext* g_ctx = nullptr;  // set once at registration

// Flash writes stall the CPU cache; keep OT bit-banging and I2C valve
// traffic quiet for the duration.
void pauseControl(bool paused) {
    g_ctx->ot.setPaused(paused);
    g_ctx->control.setPaused(paused);
}

void resetSession(bool success) {
    const bool shouldRemount = session.fsUnmounted && !success;
    session = OtaSession();
    if (shouldRemount) {
        remountLittleFs();
    }
    if (!success) {
        pauseControl(false);
    }
}

bool parseMode(const std::string& rawMode, OtaMode& mode) {
    const std::string m = StringUtils::toLower(rawMode);
    if (m == "fr" || m == "fw" || m == "firmware" || m == "flash") {
        mode = OtaMode::Firmware;
        return true;
    }
    if (m == "fs" || m == "filesystem" || m == "spiffs" || m == "littlefs") {
        mode = OtaMode::Filesystem;
        return true;
    }
    return false;
}

std::string getQueryParam(httpd_req_t* req, const char* key) {
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0) return "";
    std::vector<char> buf(qlen + 1);
    if (httpd_req_get_url_query_str(req, buf.data(), buf.size()) != ESP_OK) return "";
    char val[64];
    if (httpd_query_key_value(buf.data(), key, val, sizeof(val)) != ESP_OK) return "";
    return StringUtils::urlDecode(val);
}

std::string getModeParam(httpd_req_t* req) {
    std::string value = getQueryParam(req, "mode");
    if (!value.empty()) return value;
    return getQueryParam(req, "fileType");
}

esp_err_t sendError(httpd_req_t* req, const std::string& message) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, message.c_str(), message.size());
}

bool beginOtaSession(OtaMode mode, std::string& errorOut) {
    if (session.active) {
        errorOut = "OTA session already in progress";
        return false;
    }
    if (g_ctx->update.busy()) {
        errorOut = "An automatic update is in progress";
        return false;
    }

    session = OtaSession();
    session.active = true;
    session.startedAtUs = esp_timer_get_time();
    session.isFilesystem = (mode == OtaMode::Filesystem);
    pauseControl(true);

    if (session.isFilesystem) {
        session.fsPartition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "littlefs");
        if (!session.fsPartition) {
            errorOut = "Unable to find the littlefs partition";
            resetSession(false);
            return false;
        }

        session.fsUnmounted = true;
        esp_vfs_littlefs_unregister("littlefs");

        esp_err_t err = esp_partition_erase_range(session.fsPartition, 0,
                                                  session.fsPartition->size);
        if (err != ESP_OK) {
            errorOut = esp_err_to_name(err);
            resetSession(false);
            return false;
        }

        ESP_LOGI(TAG, "Filesystem update requested, partition size: %u bytes",
                 static_cast<unsigned>(session.fsPartition->size));
    } else {
        session.otaPartition = esp_ota_get_next_update_partition(nullptr);
        if (!session.otaPartition) {
            errorOut = "No free OTA partition available";
            resetSession(false);
            return false;
        }

        esp_err_t err = esp_ota_begin(session.otaPartition, OTA_SIZE_UNKNOWN,
                                      &session.otaHandle);
        if (err != ESP_OK) {
            errorOut = esp_err_to_name(err);
            resetSession(false);
            return false;
        }

        ESP_LOGI(TAG, "Firmware update requested, target partition: %s",
                 session.otaPartition->label);
    }

    ESP_LOGI(TAG, "Update session initialized");
    return true;
}

esp_err_t handleOtaStart(httpd_req_t* req) {
    OtaMode mode = OtaMode::Firmware;
    const std::string modeParam = getModeParam(req);
    if (!modeParam.empty() && !parseMode(modeParam, mode)) {
        return sendError(req, "Invalid OTA mode parameter");
    }

    std::string error;
    if (!beginOtaSession(mode, error)) {
        return sendError(req, error);
    }

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "OK", 2);
}

esp_err_t handleUpdatePost(httpd_req_t* req) {
    if (!session.active) {
        // Client didn't call /ota/start first - infer the mode the same way.
        OtaMode mode = OtaMode::Firmware;
        const std::string modeParam = getModeParam(req);
        if (!modeParam.empty()) {
            parseMode(modeParam, mode);
        }
        std::string error;
        if (!beginOtaSession(mode, error)) {
            return sendError(req, error);
        }
    }

    char buf[1024];
    int remaining = static_cast<int>(req->content_len);
    bool hasError = false;
    std::string errorMessage;

    while (remaining > 0) {
        const int toRead = remaining < static_cast<int>(sizeof(buf))
                               ? remaining
                               : static_cast<int>(sizeof(buf));
        const int received = httpd_req_recv(req, buf, toRead);
        if (received <= 0) {
            hasError = true;
            errorMessage = "Connection error during upload";
            break;
        }

        esp_err_t err;
        if (session.isFilesystem) {
            err = esp_partition_write(session.fsPartition, session.fsWriteOffset,
                                      buf, received);
            if (err == ESP_OK) {
                session.fsWriteOffset += received;
            }
        } else {
            err = esp_ota_write(session.otaHandle, buf, received);
        }

        if (err != ESP_OK) {
            hasError = true;
            errorMessage = esp_err_to_name(err);
            break;
        }

        session.receivedBytes += received;
        remaining -= received;
    }

    if (hasError) {
        ESP_LOGE(TAG, "Write error after %u bytes: %s",
                 static_cast<unsigned>(session.receivedBytes), errorMessage.c_str());
        if (!session.isFilesystem && session.otaHandle) {
            esp_ota_end(session.otaHandle);
        }
        resetSession(false);
        return sendError(req, errorMessage);
    }

    if (!session.isFilesystem) {
        esp_err_t err = esp_ota_end(session.otaHandle);
        if (err == ESP_OK) {
            err = esp_ota_set_boot_partition(session.otaPartition);
        }
        if (err != ESP_OK) {
            const std::string finalizeError = esp_err_to_name(err);
            ESP_LOGE(TAG, "Finalize failed: %s", finalizeError.c_str());
            resetSession(false);
            return sendError(req, finalizeError);
        }
    }

    ESP_LOGI(TAG, "Update finished successfully in %lld ms (%u bytes)",
             (esp_timer_get_time() - session.startedAtUs) / 1000,
             static_cast<unsigned>(session.receivedBytes));

    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/plain");
    esp_err_t sendResult = httpd_resp_send(req, "OK", 2);
    scheduleReboot(1000);
    resetSession(true);  // control stays paused; the reboot supersedes it
    return sendResult;
}

}  // namespace

bool manualOtaActive() { return session.active; }

void registerOtaRoutes(httpd_handle_t server, WebContext& ctx) {
    g_ctx = &ctx;
    static httpd_uri_t otaStart = {
        .uri = "/ota/start", .method = HTTP_GET, .handler = handleOtaStart, .user_ctx = nullptr};
    static httpd_uri_t updatePost = {
        .uri = "/update", .method = HTTP_POST, .handler = handleUpdatePost, .user_ctx = nullptr};

    httpd_register_uri_handler(server, &otaStart);
    httpd_register_uri_handler(server, &updatePost);
}
