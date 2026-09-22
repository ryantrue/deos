// SPDX-License-Identifier: Apache-2.0

#include "ui_navigation.hpp"

#include <algorithm>

namespace deos::ui {

const char* screen_name(ScreenId screen) noexcept {
    switch (screen) {
        case ScreenId::FirstRunWelcome: return "FirstRunWelcome";
        case ScreenId::FirstRunNetwork: return "FirstRunNetwork";
        case ScreenId::FirstRunStorage: return "FirstRunStorage";
        case ScreenId::FirstRunReady: return "FirstRunReady";
        case ScreenId::Home: return "Home";
        case ScreenId::QuickSettings: return "QuickSettings";
        case ScreenId::Settings: return "Settings";
        case ScreenId::Ai: return "AI";
        case ScreenId::Control: return "Control";
        case ScreenId::Automations: return "Automations";
        case ScreenId::Apps: return "Apps";
        case ScreenId::System: return "System";
        case ScreenId::Display: return "Display";
        case ScreenId::Update: return "Update";
        case ScreenId::Developer: return "Developer";
        case ScreenId::Network: return "Network";
        case ScreenId::WifiScan: return "WifiScan";
        case ScreenId::WifiCredentials: return "WifiCredentials";
        case ScreenId::ForgetWifiConfirm: return "ForgetWifiConfirm";
        case ScreenId::Storage: return "Storage";
        case ScreenId::FormatConfirm: return "FormatConfirm";
    }
    return "Unknown";
}

Navigator::Navigator(ScreenId initial) noexcept : current_(initial) {}

ScreenId Navigator::current() const noexcept {
    return current_;
}

std::size_t Navigator::depth() const noexcept {
    return depth_;
}

bool Navigator::navigate(ScreenId target) noexcept {
    if (target == current_) {
        return false;
    }

    if (depth_ < stack_.size()) {
        stack_[depth_++] = current_;
    } else {
        std::move(stack_.begin() + 1, stack_.end(), stack_.begin());
        stack_.back() = current_;
    }

    current_ = target;
    return true;
}

ScreenId Navigator::home() noexcept {
    depth_ = 0;
    current_ = ScreenId::Home;
    return current_;
}

ScreenId Navigator::back() noexcept {
    if (depth_ == 0) {
        return home();
    }

    current_ = stack_[--depth_];
    return current_;
}

}  // namespace deos::ui
