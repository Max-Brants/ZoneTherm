#include "EthDriver.h"

#include <cstdio>

#include "esp_log.h"

namespace {
const char* TAG = "EthDriver";
}

#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_mac.h"

namespace {

// W5500 SPI nets on the ZoneTherm PCB (see U3 on ZoneTherm.kicad_pcb).
constexpr int kPhyCs = 10;
constexpr int kPhyIrq = 14;
constexpr int kPhyRst = -1;
constexpr int kSpiSck = 12;
constexpr int kSpiMiso = 13;
constexpr int kSpiMosi = 11;
// Default is 20MHz; dropped while debugging link flapping in case the
// wiring can't reliably hold the full speed.
constexpr int kSpiFreqMhz = 20;
constexpr auto kSpiHost = SPI2_HOST;

}  // namespace

void EthDriver::onEthEvent(void* arg, esp_event_base_t, int32_t id, void* data) {
    auto* self = static_cast<EthDriver*>(arg);
    switch (id) {
        case ETHERNET_EVENT_CONNECTED: {
            esp_eth_handle_t handle = *static_cast<esp_eth_handle_t*>(data);
            esp_eth_ioctl(handle, ETH_CMD_G_MAC_ADDR, self->mac_);
            ESP_LOGI(TAG, "link up");
            break;
        }
        case ETHERNET_EVENT_DISCONNECTED:
        case ETHERNET_EVENT_STOP:
            self->gotIp_ = false;
            ESP_LOGI(TAG, "link down");
            break;
        default:
            break;
    }
}

void EthDriver::onIpEvent(void* arg, esp_event_base_t, int32_t id, void* data) {
    auto* self = static_cast<EthDriver*>(arg);
    if (id == IP_EVENT_ETH_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(data);
        self->ipInfo_ = event->ip_info;
        self->gotIp_ = true;
        char buf[16];
        esp_ip4addr_ntoa(&self->ipInfo_.ip, buf, sizeof(buf));
        ESP_LOGI(TAG, "got IP %s", buf);
    } else if (id == IP_EVENT_ETH_LOST_IP) {
        self->gotIp_ = false;
        ESP_LOGI(TAG, "lost IP");
    }
}

void EthDriver::begin() {
    spi_bus_config_t busConfig = {};
    busConfig.mosi_io_num = kSpiMosi;
    busConfig.miso_io_num = kSpiMiso;
    busConfig.sclk_io_num = kSpiSck;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    esp_err_t err = spi_bus_initialize(kSpiHost, &busConfig, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return;
    }

    spi_device_interface_config_t spiDeviceConfig = {};
    spiDeviceConfig.mode = 0;
    spiDeviceConfig.clock_speed_hz = kSpiFreqMhz * 1000 * 1000;
    spiDeviceConfig.queue_size = 20;
    spiDeviceConfig.spics_io_num = kPhyCs;

    eth_w5500_config_t w5500Config = ETH_W5500_DEFAULT_CONFIG(kSpiHost, &spiDeviceConfig);
    w5500Config.int_gpio_num = kPhyIrq;

    eth_mac_config_t macConfig = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500Config, &macConfig);

    eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
    phyConfig.phy_addr = 0;
    phyConfig.reset_gpio_num = kPhyRst;
    esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phyConfig);

    esp_eth_config_t ethConfig = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t ethHandle = nullptr;
    err = esp_eth_driver_install(&ethConfig, &ethHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "driver install FAILED - check SPI wiring/CS/IRQ pins and W5500 power: %s",
                 esp_err_to_name(err));
        return;
    }

    // The W5500 has no factory-burned MAC (unlike the ESP32's internal EMAC),
    // and neither esp_eth_driver_install nor the W5500 driver programs one -
    // the chip comes up with an all-zero SHAR. Left as-is the link comes up
    // but every DHCP DISCOVER goes out with source MAC 00:00:00:00:00:00,
    // which routers/switches drop, so DHCP never completes. Derive a stable
    // per-chip MAC from the ESP32 efuse base MAC and program it, exactly as
    // the ESP-IDF SPI-Ethernet examples do (Arduino's Ethernet.begin(mac,...)
    // used to force this on us; the IDF port had dropped it).
    uint8_t macAddr[6];
    esp_read_mac(macAddr, ESP_MAC_ETH);
    err = esp_eth_ioctl(ethHandle, ETH_CMD_S_MAC_ADDR, macAddr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set MAC FAILED: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "MAC set to %02X:%02X:%02X:%02X:%02X:%02X",
                 macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
    }

    esp_netif_config_t netifConfig = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t* netif = esp_netif_new(&netifConfig);
    esp_netif_attach(netif, esp_eth_new_netif_glue(ethHandle));

    esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                        &EthDriver::onEthEvent, this, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                        &EthDriver::onIpEvent, this, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_LOST_IP,
                                        &EthDriver::onIpEvent, this, nullptr);

    err = esp_eth_start(ethHandle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "started, waiting for link/DHCP");
    } else {
        ESP_LOGE(TAG, "esp_eth_start FAILED: %s", esp_err_to_name(err));
    }
}

std::string EthDriver::ip() const {
    char buf[16] = {0};
    esp_ip4addr_ntoa(&ipInfo_.ip, buf, sizeof(buf));
    return buf;
}

std::string EthDriver::mac() const {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac_[0], mac_[1], mac_[2], mac_[3], mac_[4], mac_[5]);
    return buf;
}
