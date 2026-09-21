// SPDX-License-Identifier: Apache-2.0

#include "resource_runtime.hpp"

#include "esp_log.h"

#include <utility>

namespace deos::platform {
namespace {
constexpr char kTag[] = "deos-runtime";
}

ResourceRuntime::ResourceRuntime(Reconciler& reconciler)
    : reconciler_(reconciler),
      mutex_(xSemaphoreCreateMutex()) {
    if (mutex_ == nullptr) {
        ESP_LOGE(kTag, "failed to create desired-state mutex");
    }
}

ResourceRuntime::~ResourceRuntime() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

bool ResourceRuntime::patch_spec(const ResourceKey& key,
                                 std::string_view field,
                                 std::string value,
                                 std::size_t max_operations) {
    if (mutex_ == nullptr ||
        xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(kTag, "desired-state lock timeout");
        return false;
    }

    ResourceMap desired = reconciler_.desired();
    const auto it = desired.find(key);
    if (it == desired.end()) {
        xSemaphoreGive(mutex_);
        ESP_LOGE(kTag, "resource not found: %s", to_string(key).c_str());
        return false;
    }

    it->second.spec[std::string(field)] = std::move(value);
    ESP_LOGI(kTag, "patch %s spec.%.*s",
             to_string(key).c_str(),
             static_cast<int>(field.size()),
             field.data());

    reconciler_.apply(std::move(desired));
    const std::size_t operations =
        reconciler_.run_until_idle(max_operations);

    bool success = false;
    const auto actual = reconciler_.actual().find(key);
    if (actual != reconciler_.actual().end()) {
        success = actual->second.status.phase == Phase::Ready;
        if (!success) {
            ESP_LOGE(kTag, "%s => %s (%s)",
                     to_string(key).c_str(),
                     to_string(actual->second.status.phase).c_str(),
                     actual->second.status.message.c_str());
        }
    }

    ESP_LOGI(kTag, "runtime reconcile: %u operation(s)",
             static_cast<unsigned>(operations));
    xSemaphoreGive(mutex_);
    return success;
}

std::string ResourceRuntime::desired_field(const ResourceKey& key,
                                           std::string_view field,
                                           std::string fallback) const {
    if (mutex_ == nullptr ||
        xSemaphoreTake(mutex_, pdMS_TO_TICKS(250)) != pdTRUE) {
        return fallback;
    }

    const auto resource = reconciler_.desired().find(key);
    if (resource != reconciler_.desired().end()) {
        const auto value = resource->second.spec.find(std::string(field));
        if (value != resource->second.spec.end()) {
            fallback = value->second;
        }
    }

    xSemaphoreGive(mutex_);
    return fallback;
}

bool ResourceRuntime::ready(const ResourceKey& key) const {
    if (mutex_ == nullptr ||
        xSemaphoreTake(mutex_, pdMS_TO_TICKS(250)) != pdTRUE) {
        return false;
    }

    const auto it = reconciler_.actual().find(key);
    const bool result =
        it != reconciler_.actual().end() &&
        it->second.status.phase == Phase::Ready;

    xSemaphoreGive(mutex_);
    return result;
}

}  // namespace deos::platform
