// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/controller.hpp"

#include <string_view>

namespace deos::platform {

class ShellController final : public Controller {
public:
    bool supports(std::string_view kind) const override;
    ResourceStatus reconcile(const Resource& desired,
                             const AppliedResource* current) override;
    ResourceStatus remove(const AppliedResource& current) override;

private:
    bool created_{false};
};

}  // namespace deos::platform
