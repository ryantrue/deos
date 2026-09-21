// SPDX-License-Identifier: Apache-2.0

#include "deos/core/action.hpp"
#include "deos/core/controller.hpp"
#include "deos/core/event_bus.hpp"
#include "deos/core/reconciler.hpp"
#include "deos/core/state.hpp"
#include "display_controller.hpp"
#include "device_preferences.hpp"
#include "network_controller.hpp"
#include "resource_runtime.hpp"
#include "shell_controller.hpp"
#include "storage_controller.hpp"
#include "telemetry_service.hpp"
#include "touch_controller.hpp"
#include "update_controller.hpp"

#include "esp_log.h"
#include "esp_ota_ops.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <memory>
#include <string>

namespace {
constexpr const char* TAG = "deos";
constexpr TickType_t kOtaStabilityDelay = pdMS_TO_TICKS(10000);

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

void mark_ota_valid_after_stability_delay(void*) {
    vTaskDelay(kOtaStabilityDelay);

    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA image marked VALID after stability delay");
    } else {
        ESP_LOGE(TAG, "failed to mark OTA image valid: %s", esp_err_to_name(err));
    }
    vTaskDelete(nullptr);
}

void schedule_ota_validation_if_healthy(const deos::Reconciler& engine) {
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

    if (!healthy) {
        ESP_LOGE(TAG, "OTA image remains pending: local health checks did not pass");
        return;
    }

    const BaseType_t scheduled = xTaskCreate(
        mark_ota_valid_after_stability_delay,
        "deos-ota-health",
        3072,
        nullptr,
        4,
        nullptr);
    if (scheduled == pdPASS) {
        ESP_LOGI(TAG, "OTA image healthy; waiting 10 seconds before marking VALID");
    } else {
        ESP_LOGE(TAG, "OTA image remains pending: could not start stability check");
    }
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "DEOS ESP32-P4 interactive OS bring-up");

    static deos::Reconciler engine;
    static deos::EventBus system_events;
    static deos::EntityRegistry entities(&system_events);
    static deos::ActionRegistry actions(&system_events);
    static deos::platform::DevicePreferences preferences;
    static deos::platform::ResourceRuntime resource_runtime(engine);
    static deos::platform::TelemetryService telemetry(entities);

    if (!preferences.initialize()) {
        ESP_LOGW(TAG, "preferences unavailable; using runtime defaults");
    }

    const int initial_brightness = preferences.brightness(72);

    if (entities.size() == 0) {
        (void)entities.register_entity(
            {"system.ready", "System ready", "System/device", ""},
            false);
        (void)entities.register_entity(
            {"system.uptime_sec", "System uptime", "Telemetry/system", "s"},
            static_cast<std::int64_t>(0));
        (void)entities.register_entity(
            {"system.internal_free_bytes", "Internal RAM free", "Telemetry/system", "B"},
            static_cast<std::int64_t>(0));
        (void)entities.register_entity(
            {"system.internal_largest_bytes", "Largest internal block", "Telemetry/system", "B"},
            static_cast<std::int64_t>(0));
        (void)entities.register_entity(
            {"system.psram_free_bytes", "PSRAM free", "Telemetry/system", "B"},
            static_cast<std::int64_t>(0));
        (void)entities.register_entity(
            {"system.tasks", "FreeRTOS tasks", "Telemetry/system", ""},
            static_cast<std::int64_t>(0));
        (void)entities.register_entity(
            {"display.brightness", "Display brightness", "Display/primary", "%"},
            static_cast<std::int64_t>(initial_brightness));
        (void)entities.register_entity(
            {"storage.sd.state", "SD card state", "Storage/sd", ""},
            std::string("unknown"));
        (void)entities.register_entity(
            {"storage.sd.total_bytes", "SD total bytes", "Storage/sd", "B"},
            static_cast<std::int64_t>(0));
        (void)entities.register_entity(
            {"storage.sd.free_bytes", "SD free bytes", "Storage/sd", "B"},
            static_cast<std::int64_t>(0));
        (void)entities.register_entity(
            {"network.connected", "Network connected", "Network/wifi", ""},
            false);
        (void)entities.register_entity(
            {"network.ip", "Network address", "Network/wifi", ""},
            std::string(""));
        (void)entities.register_entity(
            {"network.mode", "Network mode", "Network/wifi", ""},
            std::string("offline"));
        (void)entities.register_entity(
            {"network.ssid", "Network SSID", "Network/wifi", ""},
            std::string(""));
    }

    if (!telemetry.start()) {
        ESP_LOGW(TAG, "system telemetry unavailable");
    }

    static auto system_controller = std::make_shared<SystemController>();
    static auto display_controller =
        std::make_shared<deos::platform::DisplayController>(preferences);
    static auto storage_controller =
        std::make_shared<deos::platform::StorageController>(entities);
    static auto network_controller =
        std::make_shared<deos::platform::NetworkController>(
            entities, actions, preferences);
    static auto touch_controller = std::make_shared<deos::platform::TouchController>();
    static auto update_controller =
        std::make_shared<deos::platform::UpdateController>(*network_controller);

    if (actions.size() == 0) {
        (void)actions.register_action(
            {
                "display.brightness.set",
                "Set display brightness",
                "Set Display/primary brightness percentage",
                "display.control",
                {"value"},
            },
            [](const deos::StateValues& args) -> deos::ActionResult {
                const auto it = args.find("value");
                if (it == args.end()) {
                    return {false, "missing value", {}};
                }

                const auto* value = std::get_if<std::int64_t>(&it->second);
                if (value == nullptr || *value < 10 || *value > 100) {
                    return {false, "brightness must be an integer from 10 to 100", {}};
                }

                if (!resource_runtime.patch_spec(
                        {"Display", "primary"},
                        "brightness",
                        std::to_string(*value))) {
                    return {false, "Display/primary reconciliation failed", {}};
                }

                (void)entities.set("display.brightness", *value);
                return {true, "brightness updated", {{"value", *value}}};
            });

        (void)actions.register_action(
            {
                "storage.sd.rescan",
                "Rescan SD card",
                "Probe the SDMMC slot without modifying card contents",
                "storage.control",
                {},
            },
            [](const deos::StateValues&) -> deos::ActionResult {
                const bool accepted = storage_controller->request_rescan();
                return {
                    accepted,
                    accepted ? "SD rescan started" : "SD rescan is not available in current state",
                    {},
                };
            });

        (void)actions.register_action(
            {
                "storage.sd.initialize",
                "Initialize SD card for DEOS",
                "Create the DEOS directory layout without deleting existing files",
                "storage.control",
                {},
            },
            [](const deos::StateValues&) -> deos::ActionResult {
                const bool accepted = storage_controller->request_initialize_for_deos();
                return {
                    accepted,
                    accepted ? "DEOS directory initialization started"
                             : "SD initialization is not available in current state",
                    {},
                };
            });

        (void)actions.register_action(
            {
                "storage.sd.format",
                "Format SD card for DEOS",
                "Erase the SD card, create a canonical partition/filesystem and initialize DEOS directories",
                "storage.destructive",
                {},
            },
            [](const deos::StateValues&) -> deos::ActionResult {
                const bool accepted = storage_controller->request_format_for_deos();
                return {
                    accepted,
                    accepted ? "SD format started"
                             : "SD format is not available in current state",
                    {},
                };
            });

        (void)actions.register_action(
            {
                "network.wifi.configure",
                "Configure Wi-Fi profile",
                "Save Wi-Fi credentials from the local trusted shell and reboot",
                "network.credentials",
                {"ssid", "password"},
            },
            [](const deos::StateValues& args) -> deos::ActionResult {
                const auto ssid_it = args.find("ssid");
                const auto password_it = args.find("password");
                if (ssid_it == args.end() || password_it == args.end()) {
                    return {false, "ssid and password are required", {}};
                }

                const auto* ssid = std::get_if<std::string>(&ssid_it->second);
                const auto* password = std::get_if<std::string>(&password_it->second);
                if (ssid == nullptr || password == nullptr ||
                    ssid->empty() || ssid->size() > 32 || password->size() > 63) {
                    return {false, "invalid Wi-Fi credentials", {}};
                }

                const bool accepted =
                    network_controller->configure_wifi_and_reboot(*ssid, *password);
                return {
                    accepted,
                    accepted ? "Wi-Fi profile saved; reboot scheduled"
                             : "Wi-Fi profile could not be saved",
                    {},
                };
            });

        (void)actions.register_action(
            {
                "network.wifi.forget",
                "Forget Wi-Fi profile",
                "Remove the saved Wi-Fi profile and reboot into provisioning",
                "network.control",
                {},
            },
            [](const deos::StateValues&) -> deos::ActionResult {
                const bool accepted = network_controller->forget_wifi_and_reboot();
                return {
                    accepted,
                    accepted ? "Wi-Fi profile removed; reboot scheduled"
                             : "Wi-Fi profile cannot be removed in current state",
                    {},
                };
            });
    }

    static auto shell_controller =
        std::make_shared<deos::platform::ShellController>(
            entities,
            actions,
            preferences,
            resource_runtime,
            *network_controller,
            *storage_controller);

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
            {"brightness", std::to_string(initial_brightness)},
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

    ESP_LOGI(TAG, "boot stage 1: local interactive system");
    for (const auto& step : engine.plan(desired)) {
        ESP_LOGI(TAG, "plan %s %s",
                 deos::to_string(step.op).c_str(),
                 deos::to_string(step.key).c_str());
    }

    // apply() takes the map by value. Keep our local desired-state copy so the
    // second boot stage can extend it without rebuilding the already-running UI.
    engine.apply(desired);
    const auto local_operations = engine.run_until_idle(128);
    ESP_LOGI(TAG, "local reconciliation complete: %u operation(s)",
             static_cast<unsigned>(local_operations));

    for (const auto& [key, resource] : engine.actual()) {
        ESP_LOGI(TAG, "%s => %s (%s)",
                 deos::to_string(key).c_str(),
                 deos::to_string(resource.status.phase).c_str(),
                 resource.status.message.c_str());
    }

    (void)entities.set(
        "system.ready",
        resource_ready(engine, "System", "device"));

    // OTA validity is deliberately a local health decision. Mark a healthy
    // display/shell/touch image valid before optional network bring-up can
    // block or fail because of a missing router/C6 firmware mismatch.
    schedule_ota_validation_if_healthy(engine);

    ESP_LOGI(TAG, "boot stage 2: optional connectivity and remote update");

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
    const auto optional_operations = engine.run_until_idle(64);
    ESP_LOGI(TAG, "optional reconciliation complete: %u operation(s)",
             static_cast<unsigned>(optional_operations));

    for (const auto& [key, resource] : engine.actual()) {
        if (key.kind == "Network" || key.kind == "Update") {
            ESP_LOGI(TAG, "%s => %s (%s)",
                     deos::to_string(key).c_str(),
                     deos::to_string(resource.status.phase).c_str(),
                     resource.status.message.c_str());
        }
    }

    const auto network_state = network_controller->snapshot();
    (void)entities.set("network.connected", network_state.connected);
    (void)entities.set("network.ip", network_state.ip);

    ESP_LOGI(TAG,
             "State + Actions ready: %u entities, %u actions",
             static_cast<unsigned>(entities.size()),
             static_cast<unsigned>(actions.size()));
}
