// WiFi STA (fallback network) and SoftAP (provisioning) modes.
// NetworkManager decides which mode runs; this driver only executes.

#pragma once

#include <string>

#include "esp_event.h"
#include "esp_netif.h"

class WifiDriver {
public:
    void startSta(const std::string& ssid, const std::string& password);
    void startAp(const std::string& ssid);  // open network, 192.168.4.1
    void stop();

    // Re-issues esp_wifi_connect() at most every 10 s while disconnected.
    void retryStaIfDue();

    bool staHasIp() const { return staGotIp_; }
    bool apActive() const { return apActive_; }
    std::string ip() const;  // STA IP, or the AP address when provisioning
    std::string mac() const;

private:
    void ensureInited();

    static void onWifiEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
    static void onIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data);

    bool inited_ = false;
    bool started_ = false;
    bool apActive_ = false;
    volatile bool staGotIp_ = false;
    esp_netif_ip_info_t ipInfo_ = {};
    int64_t lastRetryUs_ = 0;
};
