#include "deos/core/controller.hpp"
#include "deos/core/reconciler.hpp"

#include "esp_log.h"

#include <memory>
#include <string>

namespace {
constexpr const char* TAG = "deos";

class BootstrapController final : public deos::Controller {
public:
    bool supports(std::string_view kind) const override {
        return kind == "System";
    }

    deos::ResourceStatus reconcile(const deos::Resource& desired,
                                   const deos::AppliedResource*) override {
        ESP_LOGI(TAG, "reconcile %s", deos::to_string(desired.key).c_str());
        return {deos::Phase::Ready, "bootstrap controller ready", desired.spec};
    }

    deos::ResourceStatus remove(const deos::AppliedResource& current) override {
        ESP_LOGI(TAG, "remove %s", deos::to_string(current.desired.key).c_str());
        return {deos::Phase::Ready, "removed", {}};
    }
};
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "DEOS ESP32-P4 bootstrap");

    deos::Reconciler engine;
    engine.register_controller(std::make_shared<BootstrapController>());

    deos::ResourceMap desired;
    desired[{"System", "device"}] = {
        {"System", "device"},
        {{"board", "waveshare-esp32-p4-wifi6-touch-lcd-4b"}, {"mode", "normal"}},
        {}
    };

    for (const auto& step : engine.plan(desired)) {
        ESP_LOGI(TAG, "plan %s %s",
                 deos::to_string(step.op).c_str(),
                 deos::to_string(step.key).c_str());
    }

    engine.apply(std::move(desired));
    const auto operations = engine.run_until_idle();
    ESP_LOGI(TAG, "reconciliation complete: %u operation(s)", static_cast<unsigned>(operations));
}
