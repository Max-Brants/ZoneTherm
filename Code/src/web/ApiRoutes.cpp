#include "ApiRoutes.h"

#include <cstdio>
#include <vector>

#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "../app/Version.h"
#include "../domain/ValvePlan.h"
#include "../services/ZoneJson.h"
#include "../util/Json.h"
#include "../util/Reboot.h"
#include "../util/StringUtils.h"
#include "OtaRoutes.h"

namespace {

const char* TAG = "ApiRoutes";

// Passwords are never echoed back; this sentinel means "unchanged" on POST.
const char* kMaskedSecret = "•••";

inline uint32_t nowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// Captures both the POST body (form-urlencoded) and the query string once
// per request, since httpd_req_recv() can only drain the body socket once -
// callers needing multiple params (e.g. /toggle's feature+enabled) must
// look them up from this rather than re-reading the request.
struct RequestParams {
    std::string body;
    std::string query;

    explicit RequestParams(httpd_req_t* req) {
        if (req->content_len > 0 && req->content_len < 1024) {
            std::vector<char> buf(req->content_len + 1);
            size_t received = 0;
            while (received < req->content_len) {
                int ret = httpd_req_recv(req, buf.data() + received,
                                         req->content_len - received);
                if (ret <= 0) break;
                received += ret;
            }
            body.assign(buf.data(), received);
        }

        size_t qlen = httpd_req_get_url_query_len(req);
        if (qlen > 0) {
            std::vector<char> buf(qlen + 1);
            if (httpd_req_get_url_query_str(req, buf.data(), buf.size()) == ESP_OK) {
                query.assign(buf.data());
            }
        }
    }

    bool get(const std::string& key, std::string& out) const {
        char val[160];
        if (!body.empty() &&
            httpd_query_key_value(body.c_str(), key.c_str(), val, sizeof(val)) == ESP_OK) {
            out = StringUtils::urlDecode(val);
            return true;
        }
        if (!query.empty() &&
            httpd_query_key_value(query.c_str(), key.c_str(), val, sizeof(val)) == ESP_OK) {
            out = StringUtils::urlDecode(val);
            return true;
        }
        return false;
    }
};

esp_err_t sendJson(httpd_req_t* req, const std::string& json, int statusCode = 200) {
    if (statusCode != 200) {
        char status[16];
        std::snprintf(status, sizeof(status), "%d", statusCode);
        httpd_resp_set_status(req, status);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.size());
}

esp_err_t sendError(httpd_req_t* req, const char* message, int statusCode = 400) {
    JsonDoc doc(cJSON_CreateObject());
    cJSON_AddStringToObject(doc.get(), "error", message);
    return sendJson(req, doc.dump(), statusCode);
}

esp_err_t sendSuccess(httpd_req_t* req) {
    return sendJson(req, "{\"status\":\"success\"}");
}

// ---- GET /api/system ------------------------------------------------------

esp_err_t handleApiSystem(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    const AppConfig cfg = ctx->config.get();

    const unsigned long uptime = nowMs() / 1000;
    char uptimeStr[48];
    std::snprintf(uptimeStr, sizeof(uptimeStr), "%lu days, %luh %lum %lus",
                  uptime / 86400, (uptime / 3600) % 24, (uptime / 60) % 60,
                  uptime % 60);

    uint32_t flashSize = 0;
    esp_flash_get_size(nullptr, &flashSize);

    JsonDoc doc(cJSON_CreateObject());
    cJSON* o = doc.get();
    cJSON_AddStringToObject(o, "hostname", cfg.net.hostname.c_str());
    cJSON_AddStringToObject(o, "ip", ctx->net.ip().c_str());
    cJSON_AddStringToObject(o, "mac", ctx->net.mac().c_str());
    cJSON_AddStringToObject(o, "network", ctx->net.stateName());
    cJSON_AddNumberToObject(o, "heap", esp_get_free_heap_size());
    cJSON_AddStringToObject(o, "uptime", uptimeStr);
    cJSON_AddNumberToObject(o, "cpuFreq", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    cJSON_AddNumberToObject(o, "flashSize", flashSize);
    cJSON_AddStringToObject(o, "sdkVersion", esp_get_idf_version());
    cJSON_AddStringToObject(o, "fwVersion", FW_VERSION);
    cJSON_AddStringToObject(o, "mode",
                            cfg.control.mode == Season::Heating ? "heating" : "cooling");
    return sendJson(req, doc.dump());
}

// ---- GET /api/thermostats --------------------------------------------------

esp_err_t handleApiThermostats(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    const AppConfig cfg = ctx->config.get();
    const auto snapshots = ctx->zones.snapshotAll();
    const uint32_t now = nowMs();

    JsonDoc doc(cJSON_CreateObject());
    cJSON* o = doc.get();
    cJSON_AddStringToObject(o, "hostname", cfg.net.hostname.c_str());
    cJSON_AddStringToObject(o, "fwVersion", FW_VERSION);
    cJSON_AddStringToObject(o, "mode",
                            cfg.control.mode == Season::Heating ? "heating" : "cooling");

    const uint8_t openValves = ctx->control.openValveMask();
    cJSON* list = cJSON_AddArrayToObject(o, "thermostats");
    for (int i = 0; i < kNumZones; i++) {
        cJSON_AddItemToArray(
            list, zoneToJson(snapshots[i], cfg.zones[i], i, cfg.control.mode,
                             ValvePlan::anyOpen(cfg.zones[i].valveMask, openValves),
                             now));
    }
    return sendJson(req, doc.dump());
}

// ---- POST /api/system/mode --------------------------------------------------

esp_err_t handleApiSystemMode(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    RequestParams params(req);
    std::string mode;
    if (!params.get("mode", mode)) {
        return sendError(req, "Missing mode parameter");
    }

    if (mode != "heating" && mode != "cooling") {
        return sendError(req, "Invalid mode");
    }
    ctx->config.mutate([&](AppConfig& cfg) {
        cfg.control.mode = mode == "cooling" ? Season::Cooling : Season::Heating;
    });
    for (int i = 0; i < kNumZones; i++) ctx->zones.markDirty(i);
    return sendSuccess(req);
}

// ---- POST /api/thermostat/{id}/{action} -------------------------------------

// Single wildcard handler since esp_http_server's matcher only does prefix
// wildcards, not per-segment captures.
esp_err_t handleThermostatAction(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);

    int id = 0;
    char action[32] = {0};
    if (std::sscanf(req->uri, "/api/thermostat/%d/%31[^?]", &id, action) != 2 ||
        id < 1 || id > kNumZones) {
        return sendError(req, "Invalid thermostat ID");
    }
    const int zone = id - 1;

    RequestParams params(req);
    const std::string actionStr(action);

    if (actionStr == "settemp") {
        std::string temp;
        if (!params.get("temp", temp)) {
            return sendError(req, "Missing temp parameter");
        }
        ctx->zones.setOverrideSetpoint(zone, StringUtils::toFloat(temp));
        return sendSuccess(req);
    }

    if (actionStr == "setname") {
        std::string name;
        if (!params.get("name", name)) {
            return sendError(req, "Missing name parameter");
        }
        name = StringUtils::trim(name);
        if (name.empty() || name.length() > 32) {
            return sendError(req, "Name must be 1-32 characters");
        }
        ctx->config.mutate([&](AppConfig& cfg) { cfg.zones[zone].name = name; });
        ctx->zones.markDirty(zone);
        return sendSuccess(req);
    }

    if (actionStr == "enable") {
        std::string enabled;
        if (!params.get("enabled", enabled)) {
            return sendError(req, "Missing enabled parameter");
        }
        const bool on = enabled == "true";
        ctx->config.mutate([&](AppConfig& cfg) { cfg.zones[zone].enabled = on; });
        ctx->zones.markDirty(zone);
        return sendSuccess(req);
    }

    return sendError(req, "Unknown action");
}

// ---- GET/POST /api/config ----------------------------------------------------

esp_err_t handleGetConfig(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    const AppConfig cfg = ctx->config.get();

    JsonDoc doc(cJSON_CreateObject());
    cJSON* o = doc.get();

    cJSON* net = cJSON_AddObjectToObject(o, "net");
    cJSON_AddStringToObject(net, "hostname", cfg.net.hostname.c_str());
    cJSON_AddStringToObject(net, "wifiSsid", cfg.net.wifiSsid.c_str());
    cJSON_AddStringToObject(net, "wifiPass",
                            cfg.net.wifiPass.empty() ? "" : kMaskedSecret);

    cJSON* mqtt = cJSON_AddObjectToObject(o, "mqtt");
    cJSON_AddBoolToObject(mqtt, "enabled", cfg.mqtt.enabled);
    cJSON_AddStringToObject(mqtt, "host", cfg.mqtt.host.c_str());
    cJSON_AddNumberToObject(mqtt, "port", cfg.mqtt.port);
    cJSON_AddStringToObject(mqtt, "user", cfg.mqtt.user.c_str());
    cJSON_AddStringToObject(mqtt, "pass",
                            cfg.mqtt.pass.empty() ? "" : kMaskedSecret);
    cJSON_AddStringToObject(mqtt, "baseTopic", cfg.mqtt.baseTopic.c_str());
    cJSON_AddStringToObject(mqtt, "discoveryPrefix", cfg.mqtt.discoveryPrefix.c_str());

    cJSON* control = cJSON_AddObjectToObject(o, "control");
    cJSON_AddStringToObject(control, "mode",
                            cfg.control.mode == Season::Heating ? "heating" : "cooling");
    jsonAddRounded(control, "hysteresisK", cfg.control.hysteresisK());

    cJSON* update = cJSON_AddObjectToObject(o, "update");
    cJSON_AddBoolToObject(update, "enabled", cfg.update.enabled);
    cJSON_AddBoolToObject(update, "autoInstall", cfg.update.autoInstall);
    cJSON_AddStringToObject(update, "repo", cfg.update.repo.c_str());
    cJSON_AddNumberToObject(update, "checkIntervalH", cfg.update.checkIntervalH);
    cJSON_AddBoolToObject(update, "includeFilesystem", cfg.update.includeFilesystem);

    cJSON* zones = cJSON_AddArrayToObject(o, "zones");
    for (int i = 0; i < kNumZones; i++) {
        cJSON* z = cJSON_CreateObject();
        cJSON_AddNumberToObject(z, "id", i + 1);
        cJSON_AddStringToObject(z, "name", cfg.zones[i].name.c_str());
        cJSON_AddBoolToObject(z, "enabled", cfg.zones[i].enabled);
        jsonAddValveArray(z, "valves", cfg.zones[i].valveMask);
        cJSON_AddItemToArray(zones, z);
    }

    cJSON_AddStringToObject(o, "controllerId", cfg.controllerId.c_str());
    return sendJson(req, doc.dump());
}

// Applies a (possibly partial) config JSON. Zone/control/MQTT changes take
// effect live; network changes are saved and answered with
// rebootRequired=true, then the device restarts 2 s later.
esp_err_t handlePostConfig(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);

    if (req->content_len == 0 || req->content_len > 4096) {
        return sendError(req, "Invalid request body");
    }
    std::vector<char> body(req->content_len + 1);
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body.data() + received,
                                 req->content_len - received);
        if (ret <= 0) return sendError(req, "Connection error");
        received += ret;
    }

    JsonDoc doc(cJSON_ParseWithLength(body.data(), received));
    if (!doc) {
        return sendError(req, "Invalid JSON");
    }
    cJSON* o = doc.get();

    // Valve sets are exclusive and this POST may be partial, so the check has
    // to run against the MERGED result - zones the body does not mention keep
    // what they have - and it has to run before mutate(), which persists
    // unconditionally and has no rollback.
    ValvePlan::Masks masks{};
    {
        const AppConfig current = ctx->config.get();
        for (int i = 0; i < kNumZones; i++) masks[i] = current.zones[i].valveMask;
    }
    bool valvesGiven = false;
    if (cJSON* zones = cJSON_GetObjectItemCaseSensitive(o, "zones")) {
        cJSON* z = nullptr;
        cJSON_ArrayForEach(z, zones) {
            const int id = static_cast<int>(json::getNumber(z, "id", 0));
            if (id < 1 || id > kNumZones) continue;
            cJSON* list = cJSON_GetObjectItemCaseSensitive(z, "valves");
            if (!cJSON_IsArray(list)) continue;  // absent = leave this zone alone
            uint8_t mask = 0;
            cJSON* v = nullptr;
            cJSON_ArrayForEach(v, list) {
                const int valve =
                    cJSON_IsNumber(v) ? static_cast<int>(v->valuedouble) : 0;
                if (valve < 1 || valve > kNumValves) {
                    return sendError(req, "Valve numbers must be 1-7");
                }
                mask |= static_cast<uint8_t>(1u << (valve - 1));
            }
            masks[id - 1] = mask;  // a repeated id wins last, like name/enabled
            valvesGiven = true;
        }
    }
    if (!ValvePlan::exclusive(masks)) {
        return sendError(req, "Each valve can belong to only one thermostat");
    }

    bool rebootRequired = false;
    bool mqttChanged = false;

    ctx->config.mutate([&](AppConfig& cfg) {
        if (cJSON* net = cJSON_GetObjectItemCaseSensitive(o, "net")) {
            const std::string hostname = json::getString(net, "hostname", cfg.net.hostname);
            const std::string ssid = json::getString(net, "wifiSsid", cfg.net.wifiSsid);
            std::string pass = json::getString(net, "wifiPass", kMaskedSecret);
            if (pass == kMaskedSecret) pass = cfg.net.wifiPass;

            rebootRequired = hostname != cfg.net.hostname ||
                             ssid != cfg.net.wifiSsid || pass != cfg.net.wifiPass;
            cfg.net.hostname = hostname;
            cfg.net.wifiSsid = ssid;
            cfg.net.wifiPass = pass;
        }

        if (cJSON* mqtt = cJSON_GetObjectItemCaseSensitive(o, "mqtt")) {
            MqttConfig next = cfg.mqtt;
            next.enabled = json::getBool(mqtt, "enabled", cfg.mqtt.enabled);
            next.host = json::getString(mqtt, "host", cfg.mqtt.host);
            next.port = static_cast<uint16_t>(json::getNumber(mqtt, "port", cfg.mqtt.port));
            next.user = json::getString(mqtt, "user", cfg.mqtt.user);
            std::string pass = json::getString(mqtt, "pass", kMaskedSecret);
            next.pass = (pass == kMaskedSecret) ? cfg.mqtt.pass : pass;
            next.baseTopic = json::getString(mqtt, "baseTopic", cfg.mqtt.baseTopic);
            next.discoveryPrefix =
                json::getString(mqtt, "discoveryPrefix", cfg.mqtt.discoveryPrefix);

            mqttChanged = next.enabled != cfg.mqtt.enabled || next.host != cfg.mqtt.host ||
                          next.port != cfg.mqtt.port || next.user != cfg.mqtt.user ||
                          next.pass != cfg.mqtt.pass || next.baseTopic != cfg.mqtt.baseTopic ||
                          next.discoveryPrefix != cfg.mqtt.discoveryPrefix;
            cfg.mqtt = next;
        }

        if (cJSON* control = cJSON_GetObjectItemCaseSensitive(o, "control")) {
            const std::string mode = json::getString(
                control, "mode",
                cfg.control.mode == Season::Heating ? "heating" : "cooling");
            cfg.control.mode = mode == "cooling" ? Season::Cooling : Season::Heating;
            const double hyst = json::getNumber(control, "hysteresisK",
                                                cfg.control.hysteresisK());
            if (hyst >= 0.0 && hyst <= 5.0) {
                cfg.control.hysteresisCentiK = static_cast<uint16_t>(hyst * 100 + 0.5);
            }
        }

        if (cJSON* update = cJSON_GetObjectItemCaseSensitive(o, "update")) {
            cfg.update.enabled = json::getBool(update, "enabled", cfg.update.enabled);
            cfg.update.autoInstall =
                json::getBool(update, "autoInstall", cfg.update.autoInstall);
            cfg.update.includeFilesystem =
                json::getBool(update, "includeFilesystem", cfg.update.includeFilesystem);

            const std::string repo =
                StringUtils::trim(json::getString(update, "repo", cfg.update.repo));
            // Only "owner/name" - the value is pasted straight into a URL.
            const size_t slash = repo.find('/');
            if (slash != std::string::npos && slash > 0 && slash + 1 < repo.size() &&
                repo.length() <= 100 &&
                repo.find_first_of(" \t?&#:") == std::string::npos) {
                cfg.update.repo = repo;
            }

            const int hours = static_cast<int>(
                json::getNumber(update, "checkIntervalH", cfg.update.checkIntervalH));
            if (hours >= 1 && hours <= 720) {
                cfg.update.checkIntervalH = static_cast<uint16_t>(hours);
            }
        }

        if (cJSON* zones = cJSON_GetObjectItemCaseSensitive(o, "zones")) {
            cJSON* z = nullptr;
            cJSON_ArrayForEach(z, zones) {
                const int id = static_cast<int>(json::getNumber(z, "id", 0));
                if (id < 1 || id > kNumZones) continue;
                ZoneConfig& zc = cfg.zones[id - 1];
                std::string name = StringUtils::trim(json::getString(z, "name", zc.name));
                if (!name.empty() && name.length() <= 32) zc.name = name;
                zc.enabled = json::getBool(z, "enabled", zc.enabled);
            }
        }

        // Applied as a set, not per entry: `masks` was seeded from the current
        // config above, so zones the body omitted are written back unchanged.
        // Seeding outside the lock is safe because this handler is the only
        // writer of valveMask - the MQTT command path only touches `enabled`.
        if (valvesGiven) {
            for (int i = 0; i < kNumZones; i++) cfg.zones[i].valveMask = masks[i];
        }
    });

    for (int i = 0; i < kNumZones; i++) ctx->zones.markDirty(i);
    if (mqttChanged && ctx->onMqttConfigChanged) {
        ctx->onMqttConfigChanged();
    }

    JsonDoc resp(cJSON_CreateObject());
    cJSON_AddStringToObject(resp.get(), "status", "success");
    cJSON_AddBoolToObject(resp.get(), "rebootRequired", rebootRequired);
    esp_err_t result = sendJson(req, resp.dump());

    if (rebootRequired) {
        ESP_LOGW(TAG, "Network config changed, rebooting in 2 s");
        scheduleReboot(2000);
    }
    return result;
}

// ---- GET /api/update, POST /api/update/check, POST /api/update/install ----

const char* updateStateName(UpdateState state) {
    switch (state) {
        case UpdateState::Checking:    return "checking";
        case UpdateState::Available:   return "available";
        case UpdateState::Downloading: return "downloading";
        case UpdateState::Failed:      return "failed";
        case UpdateState::Installed:   return "installed";
        case UpdateState::Idle:        break;
    }
    return "idle";
}

esp_err_t handleGetUpdate(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    const UpdateStatus st = ctx->update.status();

    JsonDoc doc(cJSON_CreateObject());
    cJSON* o = doc.get();
    cJSON_AddStringToObject(o, "state", updateStateName(st.state));
    cJSON_AddStringToObject(o, "installedVersion", st.installedVersion.c_str());
    cJSON_AddStringToObject(o, "latestVersion", st.latestVersion.c_str());
    cJSON_AddStringToObject(o, "releaseUrl", st.releaseUrl.c_str());
    cJSON_AddStringToObject(o, "message", st.message.c_str());
    cJSON_AddNumberToObject(o, "progress", st.progressPct);
    cJSON_AddBoolToObject(o, "updateAvailable", st.updateAvailable);
    cJSON_AddBoolToObject(o, "autoInstall", st.autoInstall);
    cJSON_AddBoolToObject(o, "pendingVerify", st.pendingVerify);
    cJSON_AddBoolToObject(o, "busy", ctx->update.busy());
    cJSON_AddBoolToObject(o, "checked", st.lastCheckUptimeS != 0);
    return sendJson(req, doc.dump());
}

esp_err_t handleUpdateCheck(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    if (!ctx->net.isConnected()) {
        return sendError(req, "No network connection");
    }
    if (!ctx->update.requestCheck()) {
        return sendError(req, "An update operation is already running", 409);
    }
    return sendSuccess(req);
}

esp_err_t handleUpdateInstall(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    if (!ctx->net.isConnected()) {
        return sendError(req, "No network connection");
    }
    if (manualOtaActive()) {
        return sendError(req, "A manual upload is in progress", 409);
    }
    if (!ctx->update.requestInstall()) {
        return sendError(req, "Nothing staged to install - run a check first", 409);
    }
    return sendSuccess(req);
}

esp_err_t handleFactoryReset(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
    ctx->config.factoryReset();
    esp_err_t result = sendSuccess(req);
    scheduleReboot(2000);
    return result;
}

}  // namespace

void registerApiRoutes(httpd_handle_t server, WebContext& ctx) {
    static httpd_uri_t routes[] = {
        {.uri = "/api/system", .method = HTTP_GET, .handler = handleApiSystem, .user_ctx = nullptr},
        {.uri = "/api/thermostats", .method = HTTP_GET, .handler = handleApiThermostats, .user_ctx = nullptr},
        {.uri = "/api/config", .method = HTTP_GET, .handler = handleGetConfig, .user_ctx = nullptr},
        {.uri = "/api/config", .method = HTTP_POST, .handler = handlePostConfig, .user_ctx = nullptr},
        {.uri = "/api/factory-reset", .method = HTTP_POST, .handler = handleFactoryReset, .user_ctx = nullptr},
        {.uri = "/api/update", .method = HTTP_GET, .handler = handleGetUpdate, .user_ctx = nullptr},
        {.uri = "/api/update/check", .method = HTTP_POST, .handler = handleUpdateCheck, .user_ctx = nullptr},
        {.uri = "/api/update/install", .method = HTTP_POST, .handler = handleUpdateInstall, .user_ctx = nullptr},
        {.uri = "/api/system/mode", .method = HTTP_POST, .handler = handleApiSystemMode, .user_ctx = nullptr},
        {.uri = "/api/thermostat/*", .method = HTTP_POST, .handler = handleThermostatAction, .user_ctx = nullptr},
    };
    for (auto& route : routes) {
        route.user_ctx = &ctx;
        httpd_register_uri_handler(server, &route);
    }
}
