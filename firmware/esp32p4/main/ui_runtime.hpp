// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

namespace deos::ui {

class UiRuntime final {
public:
    static UiRuntime& instance();

    bool initialize(lv_display_t* display);
    bool ready() const noexcept;
    lv_display_t* display() const noexcept;

    bool lock(TickType_t timeout = portMAX_DELAY);
    void unlock();

private:
    UiRuntime() = default;

    lv_display_t* display_{nullptr};
    SemaphoreHandle_t mutex_{nullptr};
};

class UiLock final {
public:
    explicit UiLock(UiRuntime& runtime, TickType_t timeout = portMAX_DELAY)
        : runtime_(runtime), locked_(runtime_.lock(timeout)) {}

    ~UiLock() {
        if (locked_) {
            runtime_.unlock();
        }
    }

    UiLock(const UiLock&) = delete;
    UiLock& operator=(const UiLock&) = delete;

    bool locked() const noexcept { return locked_; }

private:
    UiRuntime& runtime_;
    bool locked_{false};
};

}  // namespace deos::ui
