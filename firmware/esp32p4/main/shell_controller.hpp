// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/action.hpp"
#include "deos/core/controller.hpp"
#include "deos/core/state.hpp"

#include <memory>
#include <string_view>

namespace deos::ui {
class ShellUi;
}

namespace deos::platform {

class DevicePreferences;
class NetworkController;
class ResourceRuntime;
class StorageController;

class ShellController final : public Controller {
public:
    ShellController(EntityRegistry& entities,
                    ActionRegistry& actions,
                    DevicePreferences& preferences,
                    ResourceRuntime& resources,
                    NetworkController& network,
                    StorageController& storage);
    ~ShellController() override;

    bool supports(std::string_view kind) const override;
    ResourceStatus reconcile(const Resource& desired,
                             const AppliedResource* current) override;
    ResourceStatus remove(const AppliedResource& current) override;

private:
    EntityRegistry& entities_;
    ActionRegistry& actions_;
    DevicePreferences& preferences_;
    ResourceRuntime& resources_;
    NetworkController& network_;
    StorageController& storage_;
    std::unique_ptr<ui::ShellUi> ui_;
};

}  // namespace deos::platform
