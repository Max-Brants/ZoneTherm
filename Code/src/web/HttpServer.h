// httpd lifecycle + the HTML/asset routes served from LittleFS with
// %PLACEHOLDER% substitution. REST and OTA routes live in ApiRoutes /
// OtaRoutes and are registered onto the same server handle.

#pragma once

#include <string>

#include "esp_http_server.h"

#include "WebContext.h"

class HttpServer {
public:
    explicit HttpServer(WebContext& ctx) : ctx_(ctx) {}

    // Mounts LittleFS, starts httpd, registers page + API + OTA routes.
    void start();

private:
    WebContext& ctx_;
    httpd_handle_t server_ = nullptr;
};

// Shared with ApiRoutes/OtaRoutes.
bool mountLittleFs();
void remountLittleFs();
std::string loadAsset(const std::string& filename);
