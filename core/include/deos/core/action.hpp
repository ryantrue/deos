// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/event_bus.hpp"
#include "deos/core/state.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deos {

struct ActionDescriptor {
    std::string id;
    std::string name;
    std::string description;
    std::string capability;
    std::vector<std::string> parameters;
};

struct ActionResult {
    bool ok{false};
    std::string message;
    StateValues output;
};

class ActionRegistry final {
public:
    using Handler = std::function<ActionResult(const StateValues&)>;

    explicit ActionRegistry(EventBus* events = nullptr);

    bool register_action(ActionDescriptor descriptor, Handler handler);
    bool unregister_action(std::string_view id);

    std::optional<ActionDescriptor> describe(std::string_view id) const;
    std::vector<ActionDescriptor> list() const;
    std::size_t size() const noexcept;

    ActionResult invoke(std::string_view id,
                        const StateValues& arguments = {}) const;

private:
    struct Entry {
        ActionDescriptor descriptor;
        Handler handler;
    };

    EventBus* events_{nullptr};
    std::map<std::string, Entry, std::less<>> actions_;
};

}  // namespace deos
