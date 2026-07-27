#include "ConfigStore.h"

#include <cstdio>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"

namespace {

const char* TAG = "ConfigStore";

constexpr const char* kNamespace = "otcfg";
constexpr const char* kLegacyNamespace = "opentherm";
constexpr uint8_t kSchemaVersion = 2;  // v2 added the update.* keys

// Where UpdateService looks for releases out of the box. Overridable from the
// web UI, so a fork only has to change it once at runtime.
constexpr const char* kDefaultRepo = "Max-Brants/ZoneTherm";

std::string readStr(nvs_handle_t handle, const char* key, const std::string& fallback) {
    size_t size = 0;
    if (nvs_get_str(handle, key, nullptr, &size) != ESP_OK || size == 0) {
        return fallback;
    }
    std::string value(size - 1, '\0');
    if (nvs_get_str(handle, key, value.data(), &size) != ESP_OK) {
        return fallback;
    }
    return value;
}

uint8_t readU8(nvs_handle_t handle, const char* key, uint8_t fallback) {
    uint8_t value = fallback;
    nvs_get_u8(handle, key, &value);
    return value;
}

uint16_t readU16(nvs_handle_t handle, const char* key, uint16_t fallback) {
    uint16_t value = fallback;
    nvs_get_u16(handle, key, &value);
    return value;
}

std::string controllerIdFromMac() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char id[7];
    std::snprintf(id, sizeof(id), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return id;
}

}  // namespace

void ConfigStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    applyDefaults();
    migrateLegacy();
    readNvs();
    ESP_LOGI(TAG, "Loaded config: id=%s host=%s wifi=%s mqtt=%s:%u (%s) mode=%s hyst=%.2fK",
             cfg_.controllerId.c_str(), cfg_.net.hostname.c_str(),
             cfg_.net.wifiSsid.empty() ? "<unset>" : cfg_.net.wifiSsid.c_str(),
             cfg_.mqtt.host.empty() ? "<unset>" : cfg_.mqtt.host.c_str(),
             cfg_.mqtt.port, cfg_.mqtt.enabled ? "enabled" : "disabled",
             cfg_.control.mode == Season::Heating ? "HEATING" : "COOLING",
             cfg_.control.hysteresisK());
}

AppConfig ConfigStore::get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cfg_;
}

void ConfigStore::mutate(const std::function<void(AppConfig&)>& fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    fn(cfg_);
    writeNvs();
}

void ConfigStore::factoryReset() {
    std::lock_guard<std::mutex> lock(mutex_);
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    applyDefaults();
    ESP_LOGW(TAG, "Factory reset: stored configuration erased");
}

void ConfigStore::applyDefaults() {
    cfg_ = AppConfig{};
    cfg_.controllerId = controllerIdFromMac();
    cfg_.net.hostname = "zonetherm-" + cfg_.controllerId;
    cfg_.mqtt.baseTopic = "zonetherm/" + cfg_.controllerId;
    cfg_.mqtt.discoveryPrefix = "homeassistant";
    cfg_.update.repo = kDefaultRepo;
    for (int i = 0; i < kNumZones; i++) {
        cfg_.zones[i].name = "Thermostat " + std::to_string(i);
    }
}

void ConfigStore::migrateLegacy() {
    // Only when the new namespace has never been written.
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t ver = 0;
        const bool haveVer = nvs_get_u8(handle, "ver", &ver) == ESP_OK;
        nvs_close(handle);
        if (haveVer) return;
    }

    nvs_handle_t legacy;
    if (nvs_open(kLegacyNamespace, NVS_READONLY, &legacy) != ESP_OK) {
        // Fresh device: just seed the schema version.
        writeNvs();
        return;
    }

    cfg_.control.mode = readU8(legacy, "sysmode", 0) == 1 ? Season::Cooling
                                                          : Season::Heating;
    for (int i = 0; i < kNumZones; i++) {
        char key[16];
        std::snprintf(key, sizeof(key), "thname%d", i);
        cfg_.zones[i].name = readStr(legacy, key, cfg_.zones[i].name);
    }
    nvs_close(legacy);

    // The legacy namespace is left in place (a few bytes); with "ver" now
    // written this import never runs again.
    writeNvs();
    ESP_LOGI(TAG, "Migrated legacy config (sysmode + thermostat names)");
}

void ConfigStore::readNvs() {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "No stored config, using defaults");
        return;
    }

    cfg_.net.wifiSsid = readStr(handle, "wifi_ssid", cfg_.net.wifiSsid);
    cfg_.net.wifiPass = readStr(handle, "wifi_pass", cfg_.net.wifiPass);
    cfg_.net.hostname = readStr(handle, "hostname", cfg_.net.hostname);

    cfg_.mqtt.enabled = readU8(handle, "mq_en", cfg_.mqtt.enabled ? 1 : 0) != 0;
    cfg_.mqtt.host = readStr(handle, "mq_host", cfg_.mqtt.host);
    cfg_.mqtt.port = readU16(handle, "mq_port", cfg_.mqtt.port);
    cfg_.mqtt.user = readStr(handle, "mq_user", cfg_.mqtt.user);
    cfg_.mqtt.pass = readStr(handle, "mq_pass", cfg_.mqtt.pass);
    cfg_.mqtt.baseTopic = readStr(handle, "mq_base", cfg_.mqtt.baseTopic);
    cfg_.mqtt.discoveryPrefix = readStr(handle, "mq_disc", cfg_.mqtt.discoveryPrefix);

    cfg_.control.mode = readU8(handle, "sysmode",
                               static_cast<uint8_t>(cfg_.control.mode)) == 1
                            ? Season::Cooling
                            : Season::Heating;
    cfg_.control.hysteresisCentiK =
        readU16(handle, "hyst_ck", cfg_.control.hysteresisCentiK);

    // Absent on a v1 store, in which case the defaults above stand.
    cfg_.update.enabled = readU8(handle, "up_en", cfg_.update.enabled ? 1 : 0) != 0;
    cfg_.update.autoInstall =
        readU8(handle, "up_auto", cfg_.update.autoInstall ? 1 : 0) != 0;
    cfg_.update.repo = readStr(handle, "up_repo", cfg_.update.repo);
    cfg_.update.checkIntervalH = readU16(handle, "up_ivl", cfg_.update.checkIntervalH);
    cfg_.update.includeFilesystem =
        readU8(handle, "up_fs", cfg_.update.includeFilesystem ? 1 : 0) != 0;

    for (int i = 0; i < kNumZones; i++) {
        char key[16];
        std::snprintf(key, sizeof(key), "z%d_name", i);
        cfg_.zones[i].name = readStr(handle, key, cfg_.zones[i].name);
        std::snprintf(key, sizeof(key), "z%d_en", i);
        cfg_.zones[i].enabled = readU8(handle, key, cfg_.zones[i].enabled ? 1 : 0) != 0;
    }

    nvs_close(handle);
}

void ConfigStore::writeNvs() const {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open for write failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_u8(handle, "ver", kSchemaVersion);
    nvs_set_str(handle, "wifi_ssid", cfg_.net.wifiSsid.c_str());
    nvs_set_str(handle, "wifi_pass", cfg_.net.wifiPass.c_str());
    nvs_set_str(handle, "hostname", cfg_.net.hostname.c_str());

    nvs_set_u8(handle, "mq_en", cfg_.mqtt.enabled ? 1 : 0);
    nvs_set_str(handle, "mq_host", cfg_.mqtt.host.c_str());
    nvs_set_u16(handle, "mq_port", cfg_.mqtt.port);
    nvs_set_str(handle, "mq_user", cfg_.mqtt.user.c_str());
    nvs_set_str(handle, "mq_pass", cfg_.mqtt.pass.c_str());
    nvs_set_str(handle, "mq_base", cfg_.mqtt.baseTopic.c_str());
    nvs_set_str(handle, "mq_disc", cfg_.mqtt.discoveryPrefix.c_str());

    nvs_set_u8(handle, "sysmode", static_cast<uint8_t>(cfg_.control.mode));
    nvs_set_u16(handle, "hyst_ck", cfg_.control.hysteresisCentiK);

    nvs_set_u8(handle, "up_en", cfg_.update.enabled ? 1 : 0);
    nvs_set_u8(handle, "up_auto", cfg_.update.autoInstall ? 1 : 0);
    nvs_set_str(handle, "up_repo", cfg_.update.repo.c_str());
    nvs_set_u16(handle, "up_ivl", cfg_.update.checkIntervalH);
    nvs_set_u8(handle, "up_fs", cfg_.update.includeFilesystem ? 1 : 0);

    for (int i = 0; i < kNumZones; i++) {
        char key[16];
        std::snprintf(key, sizeof(key), "z%d_name", i);
        nvs_set_str(handle, key, cfg_.zones[i].name.c_str());
        std::snprintf(key, sizeof(key), "z%d_en", i);
        nvs_set_u8(handle, key, cfg_.zones[i].enabled ? 1 : 0);
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}
