#include "TimeService.h"

#include "esp_netif_sntp.h"

namespace TimeService {

void begin() {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.nist.gov"));
    config.wait_for_sync = false;
    esp_netif_sntp_init(&config);
}

}  // namespace TimeService
