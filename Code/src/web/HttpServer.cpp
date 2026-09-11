#include "HttpServer.h"

#include <cstdio>

#include "esp_littlefs.h"
#include "esp_log.h"

#include "../util/Reboot.h"
#include "ApiRoutes.h"
#include "OtaRoutes.h"

namespace {

const char* TAG = "HttpServer";
const char* kMountPoint = "/littlefs";
const char* kPartitionLabel = "littlefs";

esp_err_t sendHtml(httpd_req_t* req, const std::string& html) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html.c_str(), html.size());
}

// All page routes serve the Svelte SPA shell (webui/); the app routes
// client-side and pulls its data from /api/*.
esp_err_t handleSpa(httpd_req_t* req) { return sendHtml(req, loadAsset("index.html")); }

esp_err_t handleRestart(httpd_req_t* req) {
    esp_err_t result = sendHtml(req, loadAsset("restart.html"));
    scheduleReboot(2000);  // give the browser time to receive the page
    return result;
}

// Streams the file straight from LittleFS in fixed-size chunks rather than
// buffering it into a std::string - app.js/style.css are big enough that a
// single contiguous heap allocation for the whole file can fail under normal
// heap fragmentation, and with C++ exceptions disabled that abort()s the
// whole device instead of just failing the request.
esp_err_t sendStaticAsset(httpd_req_t* req, const char* file, const char* type) {
    const std::string fullPath = std::string(kMountPoint) + "/" + file;
    FILE* f = fopen(fullPath.c_str(), "rb");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open asset: %s", fullPath.c_str());
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Asset not found. Flash the filesystem image.");
    }

    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");

    char buf[1024];
    size_t bytesRead;
    esp_err_t err = ESP_OK;
    while ((bytesRead = fread(buf, 1, sizeof(buf), f)) > 0) {
        err = httpd_resp_send_chunk(req, buf, bytesRead);
        if (err != ESP_OK) break;
    }
    fclose(f);
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, nullptr, 0);  // terminate chunked response
    }
    return err;
}

esp_err_t handleStyleCss(httpd_req_t* req) { return sendStaticAsset(req, "style.css", "text/css"); }
esp_err_t handleAppJs(httpd_req_t* req) { return sendStaticAsset(req, "app.js", "application/javascript"); }
esp_err_t handleFavicon(httpd_req_t* req) { return sendStaticAsset(req, "favicon.svg", "image/svg+xml"); }

esp_err_t handleNotFound(httpd_req_t* req, httpd_err_code_t) {
    const std::string html = loadAsset("404.html");
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html.c_str(), html.size());
}

}  // namespace

bool mountLittleFs() {
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = kMountPoint;
    conf.partition_label = kPartitionLabel;
    conf.format_if_mount_failed = true;

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void remountLittleFs() {
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = kMountPoint;
    conf.partition_label = kPartitionLabel;
    conf.format_if_mount_failed = false;

    if (esp_vfs_littlefs_register(&conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remount LittleFS after OTA failure. "
                      "Web assets may be unavailable until restart.");
    } else {
        ESP_LOGI(TAG, "LittleFS remounted after OTA failure.");
    }
}

std::string loadAsset(const std::string& filename) {
    const std::string fullPath = std::string(kMountPoint) + "/" + filename;

    FILE* file = fopen(fullPath.c_str(), "rb");
    if (!file) {
        ESP_LOGW(TAG, "Failed to open asset: %s", fullPath.c_str());
        return "<!DOCTYPE html><html><head><title>Error</title></head><body>"
               "<h1>Asset Not Found</h1><p>The file '" + filename +
               "' could not be loaded. Flash the filesystem image.</p></body></html>";
    }

    std::string content;
    char buf[512];
    size_t bytesRead;
    while ((bytesRead = fread(buf, 1, sizeof(buf), file)) > 0) {
        content.append(buf, bytesRead);
    }
    fclose(file);
    return content;
}

void HttpServer::start() {
    if (!mountLittleFs()) {
        ESP_LOGW(TAG, "Web assets unavailable, pages will show placeholders");
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // 9 page routes + 10 API + 2 OTA = 21 today. httpd silently refuses to
    // register past this cap, so keep headroom for the next route.
    config.max_uri_handlers = 26;
    config.uri_match_fn = httpd_uri_match_wildcard;
    // Default 4096 is too small: handlers build std::string/cJSON payloads
    // and the OTA upload keeps a 1KB buffer on this stack while calling into
    // esp_ota_write/esp_partition_write.
    config.stack_size = 10240;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    static httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handleSpa, .user_ctx = &ctx_},
        {.uri = "/thermostats", .method = HTTP_GET, .handler = handleSpa, .user_ctx = &ctx_},  // pre-redesign URL of /diagnostics
        {.uri = "/diagnostics", .method = HTTP_GET, .handler = handleSpa, .user_ctx = &ctx_},
        {.uri = "/config", .method = HTTP_GET, .handler = handleSpa, .user_ctx = &ctx_},
        {.uri = "/update", .method = HTTP_GET, .handler = handleSpa, .user_ctx = &ctx_},
        {.uri = "/restart", .method = HTTP_GET, .handler = handleRestart, .user_ctx = &ctx_},
        {.uri = "/style.css", .method = HTTP_GET, .handler = handleStyleCss, .user_ctx = &ctx_},
        {.uri = "/app.js", .method = HTTP_GET, .handler = handleAppJs, .user_ctx = &ctx_},
        {.uri = "/favicon.svg", .method = HTTP_GET, .handler = handleFavicon, .user_ctx = &ctx_},
    };
    for (auto& route : routes) {
        httpd_register_uri_handler(server_, &route);
    }
    httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND, handleNotFound);

    registerApiRoutes(server_, ctx_);
    registerOtaRoutes(server_, ctx_);

    ESP_LOGI(TAG, "Web server started");
}
