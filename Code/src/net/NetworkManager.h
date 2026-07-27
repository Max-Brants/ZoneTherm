// Event-armed network state machine: Ethernet primary, WiFi STA fallback,
// SoftAP provisioning when nothing else is possible. begin() returns
// immediately (the old 4 s blocking link wait is gone); tick() runs the
// transitions at 1 Hz from the main loop.
//
// Flow: boot -> wait up to 10 s for Ethernet DHCP. No Ethernet: STA if WiFi
// creds are stored, else SoftAP "ZoneTherm-<ID>". STA that never gets an IP
// within 60 s falls back to SoftAP. Ethernet coming up at ANY time wins and
// shuts WiFi down.

#pragma once

#include <string>

#include "../services/ConfigStore.h"
#include "EthDriver.h"
#include "WifiDriver.h"

class NetworkManager {
public:
    enum class State { EthWait, Ethernet, WifiSta, SoftAp };

    explicit NetworkManager(ConfigStore& config);

    void begin();  // netif + event loop + Ethernet start; non-blocking
    void tick();

    bool isConnected() const;
    State state() const { return state_; }
    const char* stateName() const;
    std::string ip() const;
    std::string mac() const;

private:
    void enter(State next);
    void startWifi();

    ConfigStore& config_;
    EthDriver eth_;
    WifiDriver wifi_;
    State state_ = State::EthWait;
    int64_t stateEnteredUs_ = 0;
    bool staEverGotIp_ = false;
};
