// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/controller.hpp"

#include <memory>
#include <string_view>

namespace deos::platform {

class DisplayController final : public Controller {
public:
    DisplayController();
    ~DisplayController() override;

    bool supports(std::string_view kind) const override;
    ResourceStatus reconcile(const Resource& desired,
                             const AppliedResource* current) override;
    ResourceStatus remove(const AppliedResource& current) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace deos::platform
