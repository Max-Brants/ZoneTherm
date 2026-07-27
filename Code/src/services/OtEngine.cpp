#include "OtEngine.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../app/Pins.h"
#include "../domain/OtResponder.h"

namespace {

const char* TAG = "OtEngine";

// OT spec allows the slave 20..800 ms; the old firmware used 40 ms.
constexpr int64_t kResponseDelayUs = 40 * 1000;
constexpr uint32_t kPollPeriodMs = 2;
constexpr int64_t kCtxRefreshUs = 1000 * 1000;

constexpr uint32_t kTaskStackBytes = 4096;
constexpr UBaseType_t kTaskPriority = 15;
constexpr BaseType_t kTaskCore = 1;

}  // namespace

OtEngine::OtEngine(ZoneRegistry& zones, ConfigStore& config)
    : zones_(zones), config_(config) {}

void OtEngine::start() {
    xTaskCreatePinnedToCore(&OtEngine::taskTrampoline, "ot_engine",
                            kTaskStackBytes, this, kTaskPriority, nullptr,
                            kTaskCore);
}

void OtEngine::taskTrampoline(void* arg) {
    static_cast<OtEngine*>(arg)->run();
}

void OtEngine::run() {
    refreshZoneContexts();

    // Channel init settles the OT line for ~1 s each; done here so boot of
    // the rest of the firmware doesn't wait the combined ~7 s.
    for (int i = 0; i < kNumZones; i++) {
        channels_[i].begin(pins::kThermIn[i], pins::kThermOut[i],
                           [this, i](uint32_t request, bool valid) {
                               onRequest(i, request, valid);
                           });
        ESP_LOGI(TAG, "Zone %d channel up (in:%d out:%d)", i + 1,
                 pins::kThermIn[i], pins::kThermOut[i]);
    }

    for (;;) {
        if (!paused_) {
            const int64_t now = esp_timer_get_time();

            if (now - lastCtxRefreshUs_ > kCtxRefreshUs) {
                refreshZoneContexts();
            }

            for (int i = 0; i < kNumZones; i++) {
                channels_[i].poll();
            }

            for (int i = 0; i < kNumZones; i++) {
                if (pending_[i].active && now >= pending_[i].dueAtUs) {
                    pending_[i].active = false;
                    channels_[i].sendResponse(pending_[i].frame);
                    ESP_LOGD(TAG, "Zone %d response sent %lld ms after due",
                             i + 1,
                             (esp_timer_get_time() - pending_[i].dueAtUs) / 1000);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kPollPeriodMs));
    }
}

void OtEngine::onRequest(int zone, uint32_t request, bool valid) {
    if (!valid) {
        zones_.noteFailedRequest(zone);
        return;
    }

    zones_.noteRequest(zone, static_cast<uint32_t>(esp_timer_get_time() / 1000));

    const ZoneSnapshot snapshot = zones_.snapshot(zone);
    const OtResponder::Result result =
        OtResponder::handle(request, snapshot, zoneCtx_[zone]);
    zones_.applyDelta(zone, result.delta);

    if (result.hasResponse) {
        pending_[zone].frame = result.response;
        pending_[zone].dueAtUs = esp_timer_get_time() + kResponseDelayUs;
        pending_[zone].active = true;
    }

    // Discovery aids for vendor-specific thermostat behavior: surface what
    // the master reports about itself and which data IDs we don't handle.
    if (ot::dataId(request) == ot::DataId::MConfigMMemberId) {
        ESP_LOGI(TAG, "Zone %d master config 0x%02x, member id %u", zone + 1,
                 (ot::dataValue(request) >> 8) & 0xFF,
                 ot::dataValue(request) & 0xFF);
    } else if (ot::msgType(result.response) == ot::MsgType::UnknownDataId) {
        ESP_LOGI(TAG, "Zone %d unhandled data id %u (type %u, data 0x%04x)",
                 zone + 1, ot::dataId(request),
                 static_cast<unsigned>(ot::msgType(request)),
                 ot::dataValue(request));
    }

    ESP_LOGD(TAG, "Zone %d request id=%u type=%u -> %s", zone + 1,
             ot::dataId(request), static_cast<unsigned>(ot::msgType(request)),
             result.hasResponse ? "reply queued" : "no reply");
}

void OtEngine::refreshZoneContexts() {
    const AppConfig cfg = config_.get();
    for (int i = 0; i < kNumZones; i++) {
        zoneCtx_[i].zoneEnabled = cfg.zones[i].enabled;
        zoneCtx_[i].mode = cfg.control.mode;
    }
    lastCtxRefreshUs_ = esp_timer_get_time();
}
