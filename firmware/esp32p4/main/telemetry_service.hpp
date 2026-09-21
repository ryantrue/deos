// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/state.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace deos::platform {

class TelemetryService final {
public:
    explicit TelemetryService(EntityRegistry& entities);
    ~TelemetryService();

    TelemetryService(const TelemetryService&) = delete;
    TelemetryService& operator=(const TelemetryService&) = delete;

    bool start();
    void stop();

private:
    static void task_entry(void* context);
    void run();
    void publish_once();

    EntityRegistry& entities_;
    TaskHandle_t task_{nullptr};
    bool running_{false};
};

}  // namespace deos::platform
