// W5500 SPI Ethernet bring-up, as an instance.

#pragma once

#include <string>

#include "esp_event.h"
#include "esp_netif.h"

class EthDriver {
public:
    // Installs the driver and starts it; link/IP arrive via events later.
    void begin();

    bool hasIp() const { return gotIp_; }
    std::string ip() const;
    std::string mac() const;

private:
    static void onEthEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
    static void onIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data);

    volatile bool gotIp_ = false;
    esp_netif_ip_info_t ipInfo_ = {};
    uint8_t mac_[6] = {};
};
