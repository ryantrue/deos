// SPDX-License-Identifier: Apache-2.0

#include "deos/core/action.hpp"

#include <utility>

namespace deos {

ActionRegistry::ActionRegistry(EventBus* events) : events_(events) {}

bool ActionRegistry::register_action(ActionDescriptor descriptor,
                                     Handler handler) {
    if (descriptor.id.empty() || !handler ||
        actions_.find(descriptor.id) != actions_.end()) {
        return false;
    }
    if (descriptor.name.empty()) {
        descriptor.name = descriptor.id;
    }

    const std::string id = descriptor.id;
    const std::string capability = descriptor.capability;
    actions_.emplace(
        id,
        Entry{
            std::move(descriptor),
            std::move(handler),
        });

    if (events_ != nullptr) {
        events_->publish({
            "action.registered",
            {
                {"id", id},
                {"capability", capability},
            },
        });
    }
    return true;
}

bool ActionRegistry::unregister_action(std::string_view id) {
    const auto it = actions_.find(id);
    if (it == actions_.end()) {
        return false;
    }

    const std::string key = it->first;
    actions_.erase(it);
    if (events_ != nullptr) {
        events_->publish({"action.removed", {{"id", key}}});
    }
    return true;
}

std::optional<ActionDescriptor> ActionRegistry::describe(
    std::string_view id) const {
    const auto it = actions_.find(id);
    if (it == actions_.end()) {
        return std::nullopt;
    }
    return it->second.descriptor;
}

std::vector<ActionDescriptor> ActionRegistry::list() const {
    std::vector<ActionDescriptor> result;
    result.reserve(actions_.size());
    for (const auto& [id, entry] : actions_) {
        (void)id;
        result.push_back(entry.descriptor);
    }
    return result;
}

std::size_t ActionRegistry::size() const noexcept {
    return actions_.size();
}

ActionResult ActionRegistry::invoke(std::string_view id,
                                    const StateValues& arguments) const {
    const auto it = actions_.find(id);
    if (it == actions_.end()) {
        return {false, "unknown action", {}};
    }

    ActionResult result = it->second.handler(arguments);
    if (events_ != nullptr) {
        events_->publish({
            "action.invoked",
            {
                {"id", it->first},
                {"ok", result.ok ? "true" : "false"},
                {"message", result.message},
            },
        });
    }
    return result;
}

}  // namespace deos
