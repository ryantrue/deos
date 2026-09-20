// SPDX-License-Identifier: Apache-2.0

#include "deos/core/controller.hpp"
#include "deos/core/reconciler.hpp"
#include "display_controller.hpp"
#include "network_controller.hpp"
#include "shell_controller.hpp"
#include "storage_controller.hpp"
#include "touch_controller.hpp"
#include "update_controller.hpp"

#include "esp_log.h"
#include "esp_ota_ops.h"

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

bool resource_ready(const deos::Reconciler& engine,
                    const char* kind,
                    const char* name) {
    const auto it = engine.actual().find({kind, name});
    return it != engine.actual().end() &&
           it->second.status.phase == deos::Phase::Ready;
}

void validate_ota_if_healthy(const deos::Reconciler& engine) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return;
    }

    esp_ota_img_states_t state{};
    if (esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }

    const bool healthy =
        resource_ready(engine, "System", "device") &&
        resource_ready(engine, "Display", "primary") &&
        resource_ready(engine, "Shell", "home") &&
        resource_ready(engine, "Input", "touch");

    if (healthy) {
        const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA image marked VALID after local health checks");
        } else {
            ESP_LOGE(TAG, "failed to mark OTA image valid: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "OTA image remains pending: local health checks did not pass");
    }
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "DEOS ESP32-P4 interactive OS bring-up");

    static deos::Reconciler engine;
    static auto system_controller = std::make_shared<SystemController>();
    static auto display_controller = std::make_shared<deos::platform::DisplayController>();
    static auto storage_controller = std::make_shared<deos::platform::StorageController>();
    static auto network_controller = std::make_shared<deos::platform::NetworkController>();
    static auto shell_controller =
        std::make_shared<deos::platform::ShellController>(
            *network_controller, *storage_controller);
    static auto touch_controller = std::make_shared<deos::platform::TouchController>();
    static auto update_controller =
        std::make_shared<deos::platform::UpdateController>(*network_controller);

    engine.register_controller(system_controller);
    engine.register_controller(display_controller);
    engine.register_controller(storage_controller);
    engine.register_controller(network_controller);
    engine.register_controller(shell_controller);
    engine.register_controller(touch_controller);
    engine.register_controller(update_controller);

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

    desired[{"Storage", "sd"}] = {
        {"Storage", "sd"},
        {
            {"mount", "/sdcard"},
            {"policy", "never-auto-format"},
            {"volume-marker", "DEOS/.volume"},
        },
        {{"Display", "primary"}}
    };

    desired[{"Shell", "home"}] = {
        {"Shell", "home"},
        {
            {"layout", "adaptive-tiles"},
            {"chrome", "mobile"},
            {"theme", "dark"},
        },
        {
            {"Display", "primary"},
            {"Storage", "sd"},
        }
    };

    desired[{"Input", "touch"}] = {
        {"Input", "touch"},
        {
            {"driver", "gt911"},
            {"mode", "polling"},
        },
        {{"Display", "primary"}}
    };

    desired[{"Network", "wifi"}] = {
        {"Network", "wifi"},
        {
            {"transport", "esp32c6-sdio"},
            {"provisioning", "auto"},
            {"hostname", "deos"},
        },
        {{"System", "device"}}
    };

    desired[{"Update", "system"}] = {
        {"Update", "system"},
        {
            {"strategy", "ab"},
            {"transport", "lan-http-dev"},
            {"rollback", "enabled"},
        },
        {{"Network", "wifi"}}
    };

    for (const auto& step : engine.plan(desired)) {
        ESP_LOGI(TAG, "plan %s %s",
                 deos::to_string(step.op).c_str(),
                 deos::to_string(step.key).c_str());
    }

    engine.apply(std::move(desired));
    const auto operations = engine.run_until_idle(128);
    ESP_LOGI(TAG, "reconciliation complete: %u operation(s)",
             static_cast<unsigned>(operations));

    for (const auto& [key, resource] : engine.actual()) {
        ESP_LOGI(TAG, "%s => %s (%s)",
                 deos::to_string(key).c_str(),
                 deos::to_string(resource.status.phase).c_str(),
                 resource.status.message.c_str());
    }

    validate_ota_if_healthy(engine);
}
