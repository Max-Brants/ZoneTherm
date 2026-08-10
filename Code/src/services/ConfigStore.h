// Runtime configuration: typed struct, NVS persistence, defaults and
// one-time migration from the legacy `opentherm` namespace. Replaces the
// compile-time constants (including credentials) that used to live in
// SystemConfig.cpp.

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "../domain/ZoneState.h"

struct NetworkConfig {
    std::string wifiSsid;   // empty = no WiFi configured -> SoftAP fallback
    std::string wifiPass;
    std::string hostname;   // default "zonetherm-<ID>"
};

struct MqttConfig {
    bool enabled = false;   // stays off until a broker host is set
    std::string host;
    uint16_t port = 1883;
    std::string user;
    std::string pass;
    std::string baseTopic;        // default "zonetherm/<ID>"
    std::string discoveryPrefix;  // default "homeassistant"
};

struct ZoneConfig {
    std::string name;       // default "Thermostat <n>"
    bool enabled = true;

    // Valve outputs this thermostat drives; bit v = valve v, V1 = bit 0. A
    // thermostat may drive several (a room with three underfloor loops) and a
    // valve belongs to at most one thermostat. 0 means the thermostat still
    // answers and still tracks its band, but opens nothing - a deliberately
    // fail-safe struct default. applyDefaults() installs the legacy 1:1
    // mapping (zone i -> valve i).
    uint8_t valveMask = 0;
};

struct ControlConfig {
    Season mode = Season::Heating;
    uint16_t hysteresisCentiK = 30;  // 0.30 K band

    float hysteresisK() const { return hysteresisCentiK / 100.0f; }
};

struct UpdateConfig {
    bool enabled = true;             // periodic GitHub release checks
    bool autoInstall = false;        // false = notify only, wait for a click
    std::string repo;                // "owner/name"; default kDefaultRepo
    uint16_t checkIntervalH = 24;
    bool includeFilesystem = true;   // also pull littlefs.bin (the web UI)
};

struct AppConfig {
    NetworkConfig net;
    MqttConfig mqtt;
    ZoneConfig zones[kNumZones];
    ControlConfig control;
    UpdateConfig update;

    // Derived from the MAC at boot, never persisted.
    std::string controllerId;
};

class ConfigStore {
public:
    // Reads NVS (namespace "otcfg"); on first boot imports the legacy
    // "opentherm" namespace (sysmode + thermostat names). Requires
    // nvs_flash_init() to have run.
    void load();

    AppConfig get() const;

    // Runs the mutator under the lock, then persists everything to NVS.
    // NVS skips writes for unchanged values, so saving all keys is cheap.
    void mutate(const std::function<void(AppConfig&)>& fn);

    // Erases the otcfg namespace and reverts the in-memory config to
    // defaults. Caller is expected to reboot shortly after.
    void factoryReset();

private:
    void applyDefaults();
    void migrateLegacy();
    void readNvs();
    void writeNvs() const;

    mutable std::mutex mutex_;
    AppConfig cfg_;
};
