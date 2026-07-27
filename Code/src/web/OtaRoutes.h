#pragma once

#include "esp_http_server.h"

#include "WebContext.h"

// WLED-style two-phase upload for firmware and filesystem images:
// GET /ota/start?mode=fw|fs, then POST /update with the raw image body.
// Pauses the OT engine and valve control while flash is being written.
void registerOtaRoutes(httpd_handle_t server, WebContext& ctx);

// True while a manual upload session is open. UpdateService and this module
// both write the OTA/littlefs partitions, so each refuses to start while the
// other holds the flash.
bool manualOtaActive();
