// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/controller.hpp"

#include <memory>
#include <string_view>

namespace deos::platform {

class DevicePreferences;

class DisplayController final : public Controller {
public:
    explicit DisplayController(DevicePreferences& preferences);
    ~DisplayController() override;

    bool supports(std::string_view kind) const override;
    ResourceStatus reconcile(const Resource& desired,
                             const AppliedResource* current) override;
    ResourceStatus remove(const AppliedResource& current) override;

private:
    DevicePreferences& preferences_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace deos::platform
