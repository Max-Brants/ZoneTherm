#include "HaDiscovery.h"

#include "esp_log.h"

#include "../app/Version.h"
#include "../util/Json.h"

namespace HaDiscovery {

namespace {

const char* TAG = "HaDiscovery";

cJSON* buildDeviceInfo(const AppConfig& cfg) {
    cJSON* device = cJSON_CreateObject();
    cJSON* ids = cJSON_AddArrayToObject(device, "identifiers");
    // Frozen at the pre-ZoneTherm prefix on purpose: HA keys the device (and
    // every entity below) on this string, so renaming it would orphan the
    // existing device and duplicate all entities. Display name/model above are
    // free to change - HA updates those in place.
    cJSON_AddItemToArray(ids, cJSON_CreateString(
        ("opentherm_controller_" + cfg.controllerId).c_str()));
    cJSON_AddStringToObject(device, "name", cfg.net.hostname.c_str());
    cJSON_AddStringToObject(device, "manufacturer", "DIY");
    cJSON_AddStringToObject(device, "model", "ZoneTherm Multi-Zone Controller");
    cJSON_AddStringToObject(device, "sw_version", FW_VERSION);
    return device;
}

void publishClimate(const AppConfig& cfg, int zone, const cJSON* device,
                    const Publisher& publish) {
    const std::string zoneTopic =
        cfg.mqtt.baseTopic + "/thermostat/" + std::to_string(zone + 1);
    // Same reason as buildDeviceInfo(): unique_id is HA's primary key for the
    // entity. Keep the legacy prefix so the rename doesn't re-create entities.
    const std::string uniqueId =
        "opentherm_" + cfg.controllerId + "_thermostat_" + std::to_string(zone + 1);

    JsonDoc doc(cJSON_CreateObject());
    cJSON* o = doc.get();
    cJSON_AddStringToObject(o, "name", cfg.zones[zone].name.c_str());
    cJSON_AddStringToObject(o, "unique_id", uniqueId.c_str());
    cJSON_AddItemToObject(o, "device", cJSON_Duplicate(device, true));

    cJSON_AddStringToObject(o, "mode_command_topic", (zoneTopic + "/mode/command").c_str());
    cJSON_AddStringToObject(o, "mode_state_topic", (zoneTopic + "/mode/state").c_str());
    cJSON_AddStringToObject(o, "temperature_command_topic", (zoneTopic + "/setpoint/command").c_str());
    cJSON_AddStringToObject(o, "temperature_state_topic", (zoneTopic + "/setpoint/state").c_str());
    cJSON_AddStringToObject(o, "current_temperature_topic", (zoneTopic + "/temperature").c_str());
    cJSON_AddStringToObject(o, "action_topic", (zoneTopic + "/action").c_str());
    cJSON_AddStringToObject(o, "json_attributes_topic", (zoneTopic + "/state").c_str());

    cJSON* modes = cJSON_AddArrayToObject(o, "modes");
    cJSON_AddItemToArray(modes, cJSON_CreateString("off"));
    cJSON_AddItemToArray(modes, cJSON_CreateString(
        cfg.control.mode == Season::Heating ? "heat" : "cool"));

    cJSON_AddNumberToObject(o, "min_temp", 5);
    cJSON_AddNumberToObject(o, "max_temp", 30);
    cJSON_AddNumberToObject(o, "temp_step", 0.5);
    cJSON_AddStringToObject(o, "availability_topic", (cfg.mqtt.baseTopic + "/status").c_str());

    publish(cfg.mqtt.discoveryPrefix + "/climate/" + uniqueId + "/config",
            doc.dump(), true);
}

struct SensorSpec {
    const char* suffix;       // unique_id suffix
    const char* nameSuffix;   // appended to the zone name
    const char* stateLeaf;    // topic leaf under the zone topic
    const char* unit;
    const char* deviceClass;  // nullptr = none
    const char* icon;         // nullptr = none
};

constexpr SensorSpec kSensors[] = {
    {"_mod", " Modulation Level", "/modulation", "%", nullptr, "mdi:fire"},
};

void publishSensors(const AppConfig& cfg, int zone, const cJSON* device,
                    const Publisher& publish) {
    const std::string zoneTopic =
        cfg.mqtt.baseTopic + "/thermostat/" + std::to_string(zone + 1);
    const std::string uniqueId =
        "opentherm_" + cfg.controllerId + "_thermostat_" + std::to_string(zone + 1);

    for (const SensorSpec& spec : kSensors) {
        JsonDoc doc(cJSON_CreateObject());
        cJSON* o = doc.get();
        cJSON_AddStringToObject(o, "name",
                                (cfg.zones[zone].name + spec.nameSuffix).c_str());
        cJSON_AddStringToObject(o, "unique_id", (uniqueId + spec.suffix).c_str());
        cJSON_AddItemToObject(o, "device", cJSON_Duplicate(device, true));
        cJSON_AddStringToObject(o, "state_topic", (zoneTopic + spec.stateLeaf).c_str());
        cJSON_AddStringToObject(o, "unit_of_measurement", spec.unit);
        if (spec.deviceClass) cJSON_AddStringToObject(o, "device_class", spec.deviceClass);
        cJSON_AddStringToObject(o, "state_class", "measurement");
        if (spec.icon) cJSON_AddStringToObject(o, "icon", spec.icon);
        cJSON_AddStringToObject(o, "availability_topic",
                                (cfg.mqtt.baseTopic + "/status").c_str());

        publish(cfg.mqtt.discoveryPrefix + "/sensor/" + uniqueId + spec.suffix + "/config",
                doc.dump(), true);
    }
}

}  // namespace

void publishAll(const AppConfig& cfg, const Publisher& publish) {
    JsonDoc device(buildDeviceInfo(cfg));
    for (int i = 0; i < kNumZones; i++) {
        publishClimate(cfg, i, device.get(), publish);
        publishSensors(cfg, i, device.get(), publish);
    }
    ESP_LOGI(TAG, "Discovery published for %d zones", kNumZones);
}

}  // namespace HaDiscovery
