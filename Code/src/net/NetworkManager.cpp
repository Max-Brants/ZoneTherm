#include "NetworkManager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"

namespace {

const char* TAG = "NetworkManager";

constexpr int64_t kEthWaitUs = 10LL * 1000 * 1000;
constexpr int64_t kStaProvisionTimeoutUs = 60LL * 1000 * 1000;

}  // namespace

NetworkManager::NetworkManager(ConfigStore& config) : config_(config) {}

void NetworkManager::begin() {
    esp_netif_init();
    esp_event_loop_create_default();
    eth_.begin();
    enter(State::EthWait);
}

void NetworkManager::enter(State next) {
    state_ = next;
    stateEnteredUs_ = esp_timer_get_time();
    ESP_LOGI(TAG, "-> %s", stateName());
}

void NetworkManager::startWifi() {
    const AppConfig cfg = config_.get();
    if (!cfg.net.wifiSsid.empty()) {
        staEverGotIp_ = false;
        wifi_.startSta(cfg.net.wifiSsid, cfg.net.wifiPass);
        enter(State::WifiSta);
    } else {
        wifi_.startAp("ZoneTherm-" + cfg.controllerId);
        enter(State::SoftAp);
    }
}

void NetworkManager::tick() {
    const int64_t inStateUs = esp_timer_get_time() - stateEnteredUs_;

    switch (state_) {
        case State::EthWait:
            if (eth_.hasIp()) {
                enter(State::Ethernet);
            } else if (inStateUs > kEthWaitUs) {
                ESP_LOGI(TAG, "no Ethernet after %llds, falling back to WiFi",
                         inStateUs / 1000000);
                startWifi();
            }
            break;

        case State::Ethernet:
            if (!eth_.hasIp()) {
                ESP_LOGW(TAG, "Ethernet lost");
                startWifi();
            }
            break;

        case State::WifiSta:
            if (eth_.hasIp()) {
                ESP_LOGI(TAG, "Ethernet restored, dropping WiFi");
                wifi_.stop();
                enter(State::Ethernet);
                break;
            }
            if (wifi_.staHasIp()) {
                staEverGotIp_ = true;
                break;
            }
            wifi_.retryStaIfDue();
            // Only provision when the stored credentials have never worked
            // this session - a dropped but previously working network should
            // keep retrying instead of opening an AP.
            if (!staEverGotIp_ && inStateUs > kStaProvisionTimeoutUs) {
                ESP_LOGW(TAG, "STA never connected, opening provisioning AP");
                const AppConfig cfg = config_.get();
                wifi_.startAp("ZoneTherm-" + cfg.controllerId);
                enter(State::SoftAp);
            }
            break;

        case State::SoftAp:
            if (eth_.hasIp()) {
                ESP_LOGI(TAG, "Ethernet up, closing provisioning AP");
                wifi_.stop();
                enter(State::Ethernet);
            }
            break;
    }
}

bool NetworkManager::isConnected() const {
    switch (state_) {
        case State::Ethernet: return eth_.hasIp();
        case State::WifiSta: return wifi_.staHasIp();
        case State::SoftAp: return true;  // reachable at 192.168.4.1
        default: return false;
    }
}

const char* NetworkManager::stateName() const {
    switch (state_) {
        case State::EthWait: return "waiting-for-ethernet";
        case State::Ethernet: return "ethernet";
        case State::WifiSta: return "wifi";
        case State::SoftAp: return "provisioning-ap";
    }
    return "?";
}

std::string NetworkManager::ip() const {
    switch (state_) {
        case State::Ethernet: return eth_.ip();
        case State::WifiSta:
        case State::SoftAp: return wifi_.ip();
        default: return "0.0.0.0";
    }
}

std::string NetworkManager::mac() const {
    switch (state_) {
        case State::Ethernet: return eth_.mac();
        default: return wifi_.mac();
    }
}
