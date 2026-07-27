// esp-mqtt lifecycle, command dispatch and state publishing. The client
// runs on esp-mqtt's own task; commands land in ZoneRegistry/ConfigStore
// through the same code paths REST uses. tick() (1 Hz, main task) drives
// the publish cadence: dirty zones immediately, everything every 30 s,
// availability every 10 s (plus a broker-side LWT the old firmware lacked).

#pragma once

#include <atomic>
#include <string>

#include "mqtt_client.h"

#include "../domain/ZoneRegistry.h"
#include "ConfigStore.h"
#include "ControlService.h"

class MqttService {
public:
    MqttService(ZoneRegistry& zones, ConfigStore& config, ControlService& control);

    void begin();    // no-op unless mqtt.enabled with a broker host set
    void tick();
    void restart();  // tear down + begin() with fresh config (live apply)

    bool isConnected() const { return connected_; }

private:
    static void eventTrampoline(void* arg, esp_event_base_t base, int32_t id, void* data);
    void onConnected();
    void onData(const std::string& topic, const std::string& payload);
    void handleZoneCommand(int zone, const std::string& topic, const std::string& payload);

    void publishZoneState(int zone, const AppConfig& cfg);
    void publishSystemMode(const AppConfig& cfg);
    bool publish(const std::string& topic, const std::string& payload, bool retained = false);

    ZoneRegistry& zones_;
    ConfigStore& config_;
    ControlService& control_;

    esp_mqtt_client_handle_t client_ = nullptr;
    std::atomic<bool> connected_{false};

    // Cached config the connection was built with; tick() compares against
    // the live config to notice season/name changes needing re-discovery.
    AppConfig activeCfg_;

    int64_t lastPublishUs_ = 0;
    int64_t lastStatusUs_ = 0;
};
