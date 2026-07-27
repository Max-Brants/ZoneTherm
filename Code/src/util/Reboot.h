// Deferred restart: lets an HTTP handler finish sending its response before
// the device goes down (the old firmware slept 4 s inside the handler,
// holding an httpd worker hostage).

#pragma once

#include "esp_system.h"
#include "esp_timer.h"

inline void scheduleReboot(uint32_t delayMs) {
    static esp_timer_handle_t timer = nullptr;
    if (!timer) {
        const esp_timer_create_args_t args = {
            .callback = [](void*) { esp_restart(); },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "reboot",
            .skip_unhandled_events = true,
        };
        esp_timer_create(&args, &timer);
    }
    esp_timer_stop(timer);
    esp_timer_start_once(timer, static_cast<uint64_t>(delayMs) * 1000);
}
