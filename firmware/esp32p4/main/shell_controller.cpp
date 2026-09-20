// SPDX-License-Identifier: Apache-2.0

#include "shell_controller.hpp"

#include "esp_log.h"
#include "ui_runtime.hpp"
#include "ui_shell.hpp"

#include <string>

namespace deos::platform {
namespace {
constexpr char kTag[] = "deos-shell";
}

bool ShellController::supports(std::string_view kind) const {
    return kind == "Shell";
}

ResourceStatus ShellController::reconcile(const Resource& desired,
                                          const AppliedResource*) {
    auto& runtime = ui::UiRuntime::instance();
    if (!runtime.ready()) {
        return {Phase::Waiting, "UI runtime is not ready", {}};
    }

    if (!created_) {
        ui::UiLock lock(runtime);
        if (!lock.locked()) {
            return {Phase::Waiting, "LVGL runtime is busy", {}};
        }

        ESP_LOGI(kTag, "Creating Shell/home");
        ui::create_home_shell(runtime.display());
        created_ = true;
    }

    std::string layout = "adaptive-tiles";
    const auto layout_it = desired.spec.find("layout");
    if (layout_it != desired.spec.end()) {
        layout = layout_it->second;
    }

    return {Phase::Ready,
            "home shell ready",
            {
                {"layout", layout},
                {"chrome", "mobile"},
                {"tiles", "live-ready"},
                {"theme", "dark"},
            }};
}

ResourceStatus ShellController::remove(const AppliedResource&) {
    return {Phase::Error,
            "hot shell removal is not implemented yet",
            {}};
}

}  // namespace deos::platform
