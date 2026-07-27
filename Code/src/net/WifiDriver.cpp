#include "WifiDriver.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

namespace {

const char* TAG = "WifiDriver";

constexpr int64_t kRetryIntervalUs = 10LL * 1000 * 1000;

}  // namespace

void WifiDriver::onWifiEvent(void* arg, esp_event_base_t, int32_t id, void*) {
    auto* self = static_cast<WifiDriver*>(arg);
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        self->staGotIp_ = false;
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "provisioning client joined the AP");
    }
}

void WifiDriver::onIpEvent(void* arg, esp_event_base_t, int32_t id, void* data) {
    auto* self = static_cast<WifiDriver*>(arg);
    if (id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(data);
        self->ipInfo_ = event->ip_info;
        self->staGotIp_ = true;
        char buf[16];
        esp_ip4addr_ntoa(&self->ipInfo_.ip, buf, sizeof(buf));
        ESP_LOGI(TAG, "STA got IP %s", buf);
    }
}

void WifiDriver::ensureInited() {
    if (inited_) return;

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&initConfig);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &WifiDriver::onWifiEvent, this, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &WifiDriver::onIpEvent, this, nullptr);
    inited_ = true;
}

void WifiDriver::startSta(const std::string& ssid, const std::string& password) {
    ensureInited();
    stop();

    wifi_config_t cfg = {};
    std::strncpy(reinterpret_cast<char*>(cfg.sta.ssid), ssid.c_str(),
                 sizeof(cfg.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(cfg.sta.password), password.c_str(),
                 sizeof(cfg.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();  // WIFI_EVENT_STA_START triggers the connect
    started_ = true;
    apActive_ = false;
    staGotIp_ = false;
    lastRetryUs_ = esp_timer_get_time();
    ESP_LOGI(TAG, "STA connecting to \"%s\"", ssid.c_str());
}

void WifiDriver::startAp(const std::string& ssid) {
    ensureInited();
    stop();

    wifi_config_t cfg = {};
    std::strncpy(reinterpret_cast<char*>(cfg.ap.ssid), ssid.c_str(),
                 sizeof(cfg.ap.ssid) - 1);
    cfg.ap.ssid_len = ssid.size();
    cfg.ap.channel = 1;
    cfg.ap.authmode = WIFI_AUTH_OPEN;
    cfg.ap.max_connection = 4;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &cfg);
    esp_wifi_start();
    started_ = true;
    apActive_ = true;
    staGotIp_ = false;
    ESP_LOGI(TAG, "provisioning AP \"%s\" up (open network, http://192.168.4.1/)",
             ssid.c_str());
}

void WifiDriver::stop() {
    if (!started_) return;
    esp_wifi_stop();
    started_ = false;
    apActive_ = false;
    staGotIp_ = false;
}

void WifiDriver::retryStaIfDue() {
    if (!started_ || apActive_ || staGotIp_) return;

    const int64_t now = esp_timer_get_time();
    if (now - lastRetryUs_ < kRetryIntervalUs) return;

    lastRetryUs_ = now;
    ESP_LOGI(TAG, "STA disconnected, retrying");
    esp_wifi_connect();
}

std::string WifiDriver::ip() const {
    if (apActive_) return "192.168.4.1";
    char buf[16] = {0};
    esp_ip4addr_ntoa(&ipInfo_.ip, buf, sizeof(buf));
    return buf;
}

std::string WifiDriver::mac() const {
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(apActive_ ? WIFI_IF_AP : WIFI_IF_STA, mac);
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}
