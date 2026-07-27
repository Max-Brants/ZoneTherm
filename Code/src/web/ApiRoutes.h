#pragma once

#include "esp_http_server.h"

#include "WebContext.h"

void registerApiRoutes(httpd_handle_t server, WebContext& ctx);
