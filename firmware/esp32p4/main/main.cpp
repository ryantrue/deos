// SPDX-License-Identifier: Apache-2.0

#include "deos/core/controller.hpp"
#include "deos/core/reconciler.hpp"
#include "display_controller.hpp"
#include "shell_controller.hpp"

#include "esp_log.h"

#include <memory>
#include <string>

namespace {
constexpr const char* TAG = "deos";

class SystemController final : public deos::Controller {
public:
    bool supports(std::string_view kind) const override {
        return kind == "System";
    }

    deos::ResourceStatus reconcile(const deos::Resource& desired,
                                   const deos::AppliedResource*) override {
        ESP_LOGI(TAG, "reconcile %s", deos::to_string(desired.key).c_str());
        return {deos::Phase::Ready, "system resource ready", desired.spec};
    }

    deos::ResourceStatus remove(const deos::AppliedResource&) override {
        return {deos::Phase::Error, "System/device cannot be hot-removed", {}};
    }
};

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "DEOS ESP32-P4 home shell v2");

    // Static lifetime is intentional: controllers own hardware/runtime handles used
    // by long-lived FreeRTOS/LVGL callbacks after app_main() returns.
    static deos::Reconciler engine;
    static auto system_controller = std::make_shared<SystemController>();
    static auto display_controller = std::make_shared<deos::platform::DisplayController>();
    static auto shell_controller = std::make_shared<deos::platform::ShellController>();

    engine.register_controller(system_controller);
    engine.register_controller(display_controller);
    engine.register_controller(shell_controller);

    deos::ResourceMap desired;
    desired[{"System", "device"}] = {
        {"System", "device"},
        {
            {"board", "waveshare-esp32-p4-wifi6-touch-lcd-4b"},
            {"mode", "normal"},
        },
        {}
    };
    desired[{"Display", "primary"}] = {
        {"Display", "primary"},
        {
            {"brightness", "72"},
            {"width", "720"},
            {"height", "720"},
            {"format", "rgb565"},
        },
        {{"System", "device"}}
    };
    desired[{"Shell", "home"}] = {
        {"Shell", "home"},
        {
            {"layout", "adaptive-tiles"},
            {"chrome", "mobile"},
            {"theme", "dark"},
        },
        {{"Display", "primary"}}
    };

    for (const auto& step : engine.plan(desired)) {
        ESP_LOGI(TAG, "plan %s %s",
                 deos::to_string(step.op).c_str(),
                 deos::to_string(step.key).c_str());
    }

    engine.apply(std::move(desired));
    const auto operations = engine.run_until_idle(32);
    ESP_LOGI(TAG, "reconciliation complete: %u operation(s)",
             static_cast<unsigned>(operations));

    for (const auto& [key, resource] : engine.actual()) {
        ESP_LOGI(TAG, "%s => %s (%s)",
                 deos::to_string(key).c_str(),
                 deos::to_string(resource.status.phase).c_str(),
                 resource.status.message.c_str());
    }
}
