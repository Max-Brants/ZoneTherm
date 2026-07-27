idf_component_register(
    SRC_DIRS "." "vendor/MCP23017" "vendor/OpenTherm" "domain" "services" "drivers" "net" "web"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_eth driver
              mqtt esp_http_server app_update esp_partition json
              esp_http_client esp_https_ota esp-tls mbedtls
              joltwallet__littlefs espressif__mdns
)
