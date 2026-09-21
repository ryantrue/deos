// SPDX-License-Identifier: Apache-2.0

#include "device_preferences.hpp"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>

namespace deos::platform {
namespace {
constexpr char kTag[] = "deos-prefs";
constexpr char kNamespace[] = "deos_prefs";
constexpr char kBrightness[] = "brightness";
constexpr char kSetupCompleted[] = "setup_done";
constexpr char kSetupStep[] = "setup_step";
constexpr char kDeveloperMode[] = "dev_mode";
}

bool DevicePreferences::initialize() {
    if (initialized_) {
        return true;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS requires reinitialization");
        if (nvs_flash_erase() != ESP_OK) {
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(err));
        return false;
    }

    initialized_ = true;
    return true;
}

int DevicePreferences::brightness(int fallback) const {
    fallback = std::clamp(fallback, 10, 100);
    if (!initialized_) {
        return fallback;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return fallback;
    }

    int32_t value = fallback;
    const esp_err_t err = nvs_get_i32(handle, kBrightness, &value);
    nvs_close(handle);

    if (err != ESP_OK) {
        return fallback;
    }
    return std::clamp(static_cast<int>(value), 10, 100);
}

bool DevicePreferences::set_brightness(int value) {
    if (!initialized_) {
        return false;
    }

    value = std::clamp(value, 10, 100);

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    int32_t current = -1;
    const esp_err_t current_err =
        nvs_get_i32(handle, kBrightness, &current);
    if (current_err == ESP_OK && current == value) {
        nvs_close(handle);
        return true;
    }

    esp_err_t err = nvs_set_i32(handle, kBrightness, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "brightness persistence failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(kTag, "brightness preference: %d%%", value);
    return true;
}

bool DevicePreferences::setup_completed() const {
    if (!initialized_) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    uint8_t value = 0;
    const esp_err_t err = nvs_get_u8(handle, kSetupCompleted, &value);
    nvs_close(handle);
    return err == ESP_OK && value == 1;
}

bool DevicePreferences::set_setup_completed(bool completed) {
    if (!initialized_) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_u8(handle, kSetupCompleted, completed ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "setup preference persistence failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    if (completed) {
        (void)set_setup_step(SetupStep::Ready);
    }

    ESP_LOGI(kTag, "first-run setup: %s", completed ? "complete" : "pending");
    return true;
}

SetupStep DevicePreferences::setup_step() const {
    if (!initialized_) {
        return SetupStep::Welcome;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return SetupStep::Welcome;
    }

    uint8_t value = static_cast<uint8_t>(SetupStep::Welcome);
    const esp_err_t err = nvs_get_u8(handle, kSetupStep, &value);
    nvs_close(handle);

    if (err != ESP_OK || value > static_cast<uint8_t>(SetupStep::Ready)) {
        return SetupStep::Welcome;
    }
    return static_cast<SetupStep>(value);
}

bool DevicePreferences::set_setup_step(SetupStep step) {
    if (!initialized_) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_u8(
        handle,
        kSetupStep,
        static_cast<uint8_t>(step));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "setup step persistence failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(kTag, "first-run setup step: %u",
             static_cast<unsigned>(step));
    return true;
}

bool DevicePreferences::developer_mode() const {
    if (!initialized_) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    uint8_t value = 0;
    const esp_err_t err = nvs_get_u8(handle, kDeveloperMode, &value);
    nvs_close(handle);
    return err == ESP_OK && value == 1;
}

bool DevicePreferences::set_developer_mode(bool enabled) {
    if (!initialized_) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_u8(handle, kDeveloperMode, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "developer mode persistence failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(kTag, "developer mode: %s", enabled ? "enabled" : "disabled");
    return true;
}

}  // namespace deos::platform
