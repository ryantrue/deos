// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>

namespace deos::ui {

enum class ScreenId {
    FirstRunWelcome,
    FirstRunNetwork,
    FirstRunStorage,
    FirstRunReady,
    Home,
    QuickSettings,
    Settings,
    Ai,
    Control,
    Automations,
    Apps,
    System,
    Display,
    Update,
    Developer,
    Network,
    WifiScan,
    WifiCredentials,
    ForgetWifiConfirm,
    Storage,
    FormatConfirm,
};

const char* screen_name(ScreenId screen) noexcept;

class Navigator final {
public:
    static constexpr std::size_t kMaxDepth = 12;

    explicit Navigator(ScreenId initial = ScreenId::Home) noexcept;

    ScreenId current() const noexcept;
    std::size_t depth() const noexcept;

    bool navigate(ScreenId target) noexcept;
    ScreenId home() noexcept;
    ScreenId back() noexcept;

private:
    ScreenId current_;
    std::array<ScreenId, kMaxDepth> stack_{};
    std::size_t depth_{0};
};

}  // namespace deos::ui
