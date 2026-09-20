// SPDX-License-Identifier: Apache-2.0

#include "shell_controller.hpp"

#include "esp_log.h"
#include "network_controller.hpp"
#include "storage_controller.hpp"
#include "ui_runtime.hpp"
#include "ui_shell.hpp"

#include <string>

namespace deos::platform {
namespace {
constexpr char kTag[] = "deos-shell";
}

ShellController::ShellController(NetworkController& network, StorageController& storage)
    : network_(network), storage_(storage) {}

ShellController::~ShellController() = default;

bool ShellController::supports(std::string_view kind) const {
    return kind == "Shell";
}

ResourceStatus ShellController::reconcile(const Resource& desired,
                                          const AppliedResource*) {
    auto& runtime = ui::UiRuntime::instance();
    if (!runtime.ready()) {
        return {Phase::Waiting, "UI runtime is not ready", {}};
    }

    if (!ui_) {
        ui::UiLock lock(runtime);
        if (!lock.locked()) {
            return {Phase::Waiting, "LVGL runtime is busy", {}};
        }

        ESP_LOGI(kTag, "Creating interactive Shell/home");
        ui_ = std::make_unique<ui::ShellUi>(runtime.display(), network_, storage_);
        ui_->create();
    }

    std::string layout = "adaptive-tiles";
    const auto layout_it = desired.spec.find("layout");
    if (layout_it != desired.spec.end()) {
        layout = layout_it->second;
    }

    return {
        Phase::Ready,
        "interactive home shell ready",
        {
            {"layout", layout},
            {"chrome", "mobile"},
            {"navigation", "touch"},
            {"settings", "network+system+storage"},
            {"theme", "dark"},
        }
    };
}

ResourceStatus ShellController::remove(const AppliedResource&) {
    return {
        Phase::Error,
        "hot shell removal is not implemented yet",
        {}
    };
}

}  // namespace deos::platform
