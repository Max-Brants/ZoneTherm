#include "ValveBank.h"

#include "esp_log.h"

#include "../domain/ZoneState.h"
#include "../vendor/MCP23017/MCP23017.h"

namespace {

const char* TAG = "ValveBank";

constexpr uint8_t kMcpAddress = 0x20;

// Zone index -> port B bit. Zone 0 sits on bit 1 (not bit 0) - that is how
// the PCB routes the valve outputs.
inline uint8_t zoneBit(int zone) { return 1u << (zone + 1); }

}  // namespace

void ValveBank::begin(uint8_t sdaPin, uint8_t sclPin) {
    i2c_master_bus_config_t busConfig = {};
    busConfig.i2c_port = -1;
    busConfig.sda_io_num = static_cast<gpio_num_t>(sdaPin);
    busConfig.scl_io_num = static_cast<gpio_num_t>(sclPin);
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;

    if (i2c_new_master_bus(&busConfig, &bus_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus, valve control disabled");
        return;
    }

    // Probe before touching the MCP23017 registers - writing to a chip that
    // doesn't ack floods the log with one error per register write.
    present_ = (i2c_master_probe(bus_, kMcpAddress, 1000) == ESP_OK);
    if (!present_) {
        ESP_LOGW(TAG, "MCP23017 not found on I2C bus, valve control disabled");
        return;
    }

    mcp_ = new MCP23017(bus_, kMcpAddress);
    mcp_->init();
    mcp_->portMode(MCP23017Port::B, 0);  // all outputs
    mcp_->writePort(MCP23017Port::B, 0x00);
    shadow_ = 0x00;
    ESP_LOGI(TAG, "MCP23017 ready, all valves closed");
}

void ValveBank::set(int zone, bool open) {
    if (!present_ || zone < 0 || zone >= kNumZones) return;

    const uint8_t next = open ? (shadow_ | zoneBit(zone))
                              : (shadow_ & ~zoneBit(zone));
    if (next == shadow_) return;

    shadow_ = next;
    mcp_->writePort(MCP23017Port::B, shadow_);
    ESP_LOGI(TAG, "Zone %d valve %s (port B = 0x%02X)", zone + 1,
             open ? "OPEN" : "CLOSED", shadow_);
}

bool ValveBank::isOpen(int zone) const {
    if (zone < 0 || zone >= kNumZones) return false;
    return shadow_ & zoneBit(zone);
}
