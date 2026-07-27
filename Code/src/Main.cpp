// Composition root: builds the object graph and runs the control loop.
// No singletons - everything below receives its dependencies explicitly.

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app/Pins.h"
#include "app/Version.h"
#include "domain/ClimateLogic.h"
#include "domain/ZoneRegistry.h"
#include "drivers/ValveBank.h"
#include "net/MdnsService.h"
#include "net/NetworkManager.h"
#include "services/ConfigStore.h"
#include "services/ControlService.h"
#include "services/MqttService.h"
#include "services/OtEngine.h"
#include "services/TimeService.h"
#include "services/UpdateService.h"
#include "web/HttpServer.h"
#include "web/OtaRoutes.h"

namespace {

const char* TAG = "Main";

void logZoneStatus(ZoneRegistry& zones, ControlService& control,
                   const AppConfig& cfg) {
    for (int i = 0; i < kNumZones; i++) {
        const ZoneSnapshot z = zones.snapshot(i);
        ESP_LOGI(TAG,
                 "Zone %d (%s): %.1f/%.1f degC %s valve=%s req=%lu fail=%lu",
                 i + 1, cfg.zones[i].name.c_str(), z.roomTemp, z.setpoint,
                 ClimateLogic::actionString(cfg.zones[i].enabled,
                                            cfg.control.mode,
                                            control.valveOpen(i)),
                 control.valveOpen(i) ? "open" : "closed",
                 static_cast<unsigned long>(z.totalRequests),
                 static_cast<unsigned long>(z.failedRequests));
    }
}

}  // namespace

extern "C" void app_main() {
    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "ZoneTherm controller %s starting", FW_VERSION);

    // Installed once, up front: OtEngine's task (OpenTherm bit-banging) and
    // the W5500 Ethernet driver both need the shared per-pin GPIO ISR
    // service, and each assumes it's already there rather than installing
    // it themselves. Left to chance, otEngine.start() below hands its task
    // to core 1 asynchronously, so whichever of that task or
    // network.begin() (running here on the main task) happens to reach its
    // gpio_isr_handler_add() first would win a boot-order race; the loser's
    // interrupt silently never gets registered (the W5500 driver in
    // particular doesn't check the call's return value).
    esp_err_t isrErr = gpio_install_isr_service(0);
    if (isrErr != ESP_OK && isrErr != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(isrErr));
    }

    static ConfigStore config;
    config.load();

    static ZoneRegistry zones;

    static ValveBank valves;
    valves.begin(pins::kI2cSda, pins::kI2cScl);

    static OtEngine otEngine(zones, config);
    otEngine.start();

    static ControlService control(zones, valves, config);

    static NetworkManager network(config);
    network.begin();
    TimeService::begin();
    MdnsService::begin(config.get().net.hostname);

    static MqttService mqtt(zones, config, control);
    mqtt.begin();

    static UpdateService updates(config, otEngine, control);
    updates.setFlashBusyProbe(manualOtaActive);
    updates.begin();

    static WebContext webContext{zones,     config,  network, control,
                                 otEngine,  updates, [] { mqtt.restart(); }};
    static HttpServer httpServer(webContext);
    httpServer.start();

    int64_t lastStatusLog = 0;
    for (;;) {
        network.tick();
        control.tick();
        mqtt.tick();
        updates.tick(network.isConnected());

        // Periodic serial diagnostic
        const int64_t now = esp_timer_get_time();
        if (now - lastStatusLog > 30LL * 1000 * 1000) {
            lastStatusLog = now;
            logZoneStatus(zones, control, config.get());
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
