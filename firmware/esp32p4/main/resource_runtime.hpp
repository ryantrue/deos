// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/reconciler.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace deos::platform {

class ResourceRuntime final {
public:
    explicit ResourceRuntime(Reconciler& reconciler);
    ~ResourceRuntime();

    ResourceRuntime(const ResourceRuntime&) = delete;
    ResourceRuntime& operator=(const ResourceRuntime&) = delete;

    bool patch_spec(const ResourceKey& key,
                    std::string_view field,
                    std::string value,
                    std::size_t max_operations = 32);

    std::string desired_field(const ResourceKey& key,
                              std::string_view field,
                              std::string fallback = {}) const;

    bool ready(const ResourceKey& key) const;

private:
    Reconciler& reconciler_;
    SemaphoreHandle_t mutex_{nullptr};
};

}  // namespace deos::platform
