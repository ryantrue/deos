// SPDX-License-Identifier: Apache-2.0

#include "telemetry_service.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace deos::platform {
namespace {

constexpr char kTag[] = "deos-telemetry";
constexpr TickType_t kInterval = pdMS_TO_TICKS(2000);
constexpr uint32_t kStackSize = 4096;
constexpr UBaseType_t kPriority = 2;

}  // namespace

TelemetryService::TelemetryService(EntityRegistry& entities)
    : entities_(entities) {}

TelemetryService::~TelemetryService() {
    stop();
}

bool TelemetryService::start() {
    if (task_ != nullptr) {
        return true;
    }

    running_ = true;
    const BaseType_t result = xTaskCreate(
        task_entry,
        "deos-telemetry",
        kStackSize,
        this,
        kPriority,
        &task_);

    if (result != pdPASS) {
        running_ = false;
        task_ = nullptr;
        ESP_LOGE(kTag, "failed to create telemetry task");
        return false;
    }

    ESP_LOGI(kTag, "system telemetry started");
    return true;
}

void TelemetryService::stop() {
    running_ = false;
    if (task_ != nullptr) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
}

void TelemetryService::task_entry(void* context) {
    auto* self = static_cast<TelemetryService*>(context);
    if (self != nullptr) {
        self->run();
    }
    vTaskDelete(nullptr);
}

void TelemetryService::run() {
    while (running_) {
        publish_once();
        vTaskDelay(kInterval);
    }
}

void TelemetryService::publish_once() {
    const std::int64_t uptime =
        static_cast<std::int64_t>(esp_timer_get_time() / 1000000ULL);
    const std::int64_t internal_free =
        static_cast<std::int64_t>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::int64_t internal_largest =
        static_cast<std::int64_t>(
            heap_caps_get_largest_free_block(
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::int64_t psram_free =
        static_cast<std::int64_t>(
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    const std::int64_t task_count =
        static_cast<std::int64_t>(uxTaskGetNumberOfTasks());

    (void)entities_.set("system.uptime_sec", uptime);
    (void)entities_.set("system.internal_free_bytes", internal_free);
    (void)entities_.set("system.internal_largest_bytes", internal_largest);
    (void)entities_.set("system.psram_free_bytes", psram_free);
    (void)entities_.set("system.tasks", task_count);
}

}  // namespace deos::platform
