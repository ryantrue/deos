// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/controller.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace deos::platform {

enum class SdVolumeState {
    Unknown,
    Absent,
    Foreign,
    Ready,
    NeedsFormat,
    Busy,
    Error,
};

struct SdVolumeSnapshot {
    SdVolumeState state{SdVolumeState::Unknown};
    uint64_t total_bytes{0};
    uint64_t free_bytes{0};
    std::string message;
    std::string operation;
    std::string card_name;
};

const char* to_string(SdVolumeState state) noexcept;

class StorageController final : public Controller {
public:
    StorageController();
    ~StorageController() override;

    bool supports(std::string_view kind) const override;
    ResourceStatus reconcile(const Resource& desired,
                             const AppliedResource* current) override;
    ResourceStatus remove(const AppliedResource& current) override;

    SdVolumeSnapshot snapshot() const;

    // Non-blocking requests. Work is executed on a dedicated FreeRTOS task.
    bool request_initialize_for_deos();
    bool request_format_for_deos();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace deos::platform
