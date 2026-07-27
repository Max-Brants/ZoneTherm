#include "MdnsService.h"

#include "esp_log.h"
#include "mdns.h"

namespace MdnsService {

void begin(const std::string& hostname) {
    if (mdns_init() != ESP_OK) {
        ESP_LOGW("MdnsService", "mDNS init failed");
        return;
    }
    mdns_hostname_set(hostname.c_str());
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    ESP_LOGI("MdnsService", "responding as %s.local", hostname.c_str());
}

}  // namespace MdnsService
