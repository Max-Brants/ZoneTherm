#include "MqttService.h"

#include <cstdio>

#include "esp_log.h"
#include "esp_timer.h"

#include "../domain/ClimateLogic.h"
#include "../util/Json.h"
#include "../util/StringUtils.h"
#include "HaDiscovery.h"
#include "ZoneJson.h"

namespace {

const char* TAG = "MqttService";

constexpr int64_t kPublishIntervalUs = 30LL * 1000 * 1000;
constexpr int64_t kStatusIntervalUs = 10LL * 1000 * 1000;

inline uint32_t nowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

std::string formatTemp(float v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

}  // namespace

MqttService::MqttService(ZoneRegistry& zones, ConfigStore& config,
                         ControlService& control)
    : zones_(zones), config_(config), control_(control) {}

void MqttService::begin() {
    activeCfg_ = config_.get();
    const MqttConfig& mqtt = activeCfg_.mqtt;
    if (!mqtt.enabled || mqtt.host.empty()) {
        ESP_LOGI(TAG, "MQTT disabled (enable it and set a broker on /config)");
        return;
    }

    static std::string clientId;  // esp-mqtt keeps pointers into its config copy
    clientId = "zonetherm_" + activeCfg_.controllerId;
    const std::string willTopic = mqtt.baseTopic + "/status";

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.hostname = mqtt.host.c_str();
    cfg.broker.address.port = mqtt.port;
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    cfg.credentials.client_id = clientId.c_str();
    if (!mqtt.user.empty()) {
        cfg.credentials.username = mqtt.user.c_str();
        cfg.credentials.authentication.password = mqtt.pass.c_str();
    }
    cfg.session.last_will.topic = willTopic.c_str();
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.retain = true;

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) {
        ESP_LOGE(TAG, "client init failed");
        return;
    }
    esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY,
                                   &MqttService::eventTrampoline, this);
    esp_mqtt_client_start(client_);
    ESP_LOGI(TAG, "connecting to %s:%u", mqtt.host.c_str(), mqtt.port);
}

void MqttService::restart() {
    if (client_) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        connected_ = false;
    }
    begin();
}

void MqttService::eventTrampoline(void* arg, esp_event_base_t, int32_t id,
                                  void* data) {
    auto* self = static_cast<MqttService*>(arg);
    auto event = static_cast<esp_mqtt_event_handle_t>(data);

    switch (static_cast<esp_mqtt_event_id_t>(id)) {
        case MQTT_EVENT_CONNECTED:
            self->connected_ = true;
            ESP_LOGI(TAG, "connected");
            self->onConnected();
            break;
        case MQTT_EVENT_DISCONNECTED:
            self->connected_ = false;
            ESP_LOGI(TAG, "disconnected");
            break;
        case MQTT_EVENT_DATA:
            self->onData(std::string(event->topic, event->topic_len),
                         std::string(event->data, event->data_len));
            break;
        default:
            break;
    }
}

void MqttService::onConnected() {
    const AppConfig cfg = config_.get();
    const std::string base = cfg.mqtt.baseTopic;

    esp_mqtt_client_subscribe(client_, (base + "/thermostat/+/setpoint/command").c_str(), 0);
    esp_mqtt_client_subscribe(client_, (base + "/thermostat/+/mode/command").c_str(), 0);
    esp_mqtt_client_subscribe(client_, (base + "/system/mode/command").c_str(), 0);

    const auto publisher = [this](const std::string& topic,
                                  const std::string& payload, bool retained) {
        return publish(topic, payload, retained);
    };
    HaDiscovery::publishAll(cfg, publisher);

    publish(base + "/status", "online", true);
    publishSystemMode(cfg);
    for (int i = 0; i < kNumZones; i++) {
        publishZoneState(i, cfg);
    }
}

void MqttService::onData(const std::string& topic, const std::string& payload) {
    ESP_LOGI(TAG, "message [%s] %s", topic.c_str(), payload.c_str());
    const AppConfig cfg = config_.get();

    if (StringUtils::endsWith(topic, "/system/mode/command")) {
        if (payload != "heating" && payload != "cooling") return;
        config_.mutate([&](AppConfig& c) {
            c.control.mode = payload == "cooling" ? Season::Cooling : Season::Heating;
        });
        for (int i = 0; i < kNumZones; i++) zones_.markDirty(i);
        // tick() notices the season change and republishes discovery + state
        return;
    }

    const std::string marker = cfg.mqtt.baseTopic + "/thermostat/";
    if (topic.rfind(marker, 0) != 0) return;
    const size_t idEnd = topic.find('/', marker.size());
    if (idEnd == std::string::npos) return;
    const int id = StringUtils::toInt(topic.substr(marker.size(), idEnd - marker.size()));
    if (id < 1 || id > kNumZones) return;

    handleZoneCommand(id - 1, topic, payload);
}

void MqttService::handleZoneCommand(int zone, const std::string& topic,
                                    const std::string& payload) {
    if (StringUtils::endsWith(topic, "/setpoint/command")) {
        zones_.setOverrideSetpoint(zone, StringUtils::toFloat(payload));
    } else if (StringUtils::endsWith(topic, "/mode/command")) {
        // HA sends heat/cool/auto/off; anything but off means "participate".
        const bool enabled = payload != "off";
        config_.mutate([&](AppConfig& c) { c.zones[zone].enabled = enabled; });
        zones_.markDirty(zone);
    }
}

void MqttService::publishZoneState(int zone, const AppConfig& cfg) {
    const ZoneSnapshot z = zones_.snapshot(zone);
    const std::string zoneTopic =
        cfg.mqtt.baseTopic + "/thermostat/" + std::to_string(zone + 1);
    const bool valveOpen = control_.valveOpen(zone);
    const bool enabled = cfg.zones[zone].enabled;

    publish(zoneTopic + "/temperature", formatTemp(z.roomTemp));
    publish(zoneTopic + "/setpoint/state", formatTemp(z.setpoint));

    const char* mode = !enabled ? "off"
                       : cfg.control.mode == Season::Heating ? "heat" : "cool";
    publish(zoneTopic + "/mode/state", mode);
    publish(zoneTopic + "/action",
            ClimateLogic::actionString(enabled, cfg.control.mode, valveOpen));

    publish(zoneTopic + "/outside_temperature", formatTemp(z.outsideTemp));
    publish(zoneTopic + "/modulation", formatTemp(z.modulation));

    JsonDoc doc(zoneToJson(z, cfg.zones[zone], zone, cfg.control.mode, valveOpen, nowMs()));
    publish(zoneTopic + "/state", doc.dump());
}

void MqttService::publishSystemMode(const AppConfig& cfg) {
    publish(cfg.mqtt.baseTopic + "/system/mode/state",
            cfg.control.mode == Season::Heating ? "heating" : "cooling", true);
}

bool MqttService::publish(const std::string& topic, const std::string& payload,
                          bool retained) {
    if (!client_ || !connected_) return false;
    return esp_mqtt_client_publish(client_, topic.c_str(), payload.c_str(), 0, 0,
                                   retained ? 1 : 0) >= 0;
}

void MqttService::tick() {
    if (!client_ || !connected_) return;

    const AppConfig cfg = config_.get();
    const int64_t now = esp_timer_get_time();

    // Season or zone-name changes require fresh discovery configs (modes
    // list / entity names live there).
    bool discoveryStale = cfg.control.mode != activeCfg_.control.mode;
    for (int i = 0; !discoveryStale && i < kNumZones; i++) {
        discoveryStale = cfg.zones[i].name != activeCfg_.zones[i].name;
    }
    if (discoveryStale) {
        activeCfg_ = cfg;
        const auto publisher = [this](const std::string& topic,
                                      const std::string& payload, bool retained) {
            return publish(topic, payload, retained);
        };
        HaDiscovery::publishAll(cfg, publisher);
        publishSystemMode(cfg);
    }

    uint8_t dirty = zones_.consumeDirtyMask();
    if (now - lastPublishUs_ > kPublishIntervalUs) {
        lastPublishUs_ = now;
        dirty = 0xFF;  // full refresh
    }
    for (int i = 0; i < kNumZones; i++) {
        if (dirty & (1u << i)) {
            publishZoneState(i, cfg);
        }
    }

    if (now - lastStatusUs_ > kStatusIntervalUs) {
        lastStatusUs_ = now;
        publish(cfg.mqtt.baseTopic + "/status", "online", true);
    }
}
