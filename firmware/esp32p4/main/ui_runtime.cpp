// SPDX-License-Identifier: Apache-2.0

#include "ui_runtime.hpp"

#include "esp_log.h"

namespace deos::ui {
namespace {
constexpr char kTag[] = "deos-ui";
}

UiRuntime& UiRuntime::instance() {
    static UiRuntime runtime;
    return runtime;
}

bool UiRuntime::initialize(lv_display_t* display) {
    if (display == nullptr) {
        return false;
    }

    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateRecursiveMutex();
        if (mutex_ == nullptr) {
            ESP_LOGE(kTag, "failed to create LVGL mutex");
            return false;
        }
    }

    display_ = display;
    ESP_LOGI(kTag, "UI runtime attached to display");
    return true;
}

bool UiRuntime::ready() const noexcept {
    return display_ != nullptr && mutex_ != nullptr;
}

lv_display_t* UiRuntime::display() const noexcept {
    return display_;
}

bool UiRuntime::lock(TickType_t timeout) {
    return mutex_ != nullptr && xSemaphoreTakeRecursive(mutex_, timeout) == pdTRUE;
}

void UiRuntime::unlock() {
    if (mutex_ != nullptr) {
        (void)xSemaphoreGiveRecursive(mutex_);
    }
}

}  // namespace deos::ui
