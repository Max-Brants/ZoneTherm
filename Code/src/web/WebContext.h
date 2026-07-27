// Dependencies the HTTP handlers need, wired once in Main.cpp and passed to
// every route via httpd's user_ctx pointer (no globals).

#pragma once

#include <functional>

#include "../domain/ZoneRegistry.h"
#include "../net/NetworkManager.h"
#include "../services/ConfigStore.h"
#include "../services/ControlService.h"
#include "../services/OtEngine.h"
#include "../services/UpdateService.h"

struct WebContext {
    ZoneRegistry& zones;
    ConfigStore& config;
    NetworkManager& net;
    ControlService& control;
    OtEngine& ot;
    UpdateService& update;

    // Set by MqttService (phase 4) so a config change can restart the MQTT
    // connection live; null until then.
    std::function<void()> onMqttConfigChanged;
};
