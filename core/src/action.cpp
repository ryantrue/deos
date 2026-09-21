// SPDX-License-Identifier: Apache-2.0

#include "deos/core/action.hpp"

#include <algorithm>
#include <utility>

namespace deos {

bool ActionContext::has_capability(std::string_view capability) const {
    if (capability.empty()) {
        return true;
    }
    return std::find(capabilities.begin(), capabilities.end(), capability) !=
               capabilities.end() ||
           std::find(capabilities.begin(), capabilities.end(), "*") !=
               capabilities.end();
}

ActionRegistry::ActionRegistry(EventBus* events) : events_(events) {}

bool ActionRegistry::register_action(ActionDescriptor descriptor,
                                     Handler handler) {
    if (descriptor.id.empty() || !handler) {
        return false;
    }
    if (descriptor.name.empty()) {
        descriptor.name = descriptor.id;
    }

    const std::string id = descriptor.id;
    const std::string capability = descriptor.capability;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (actions_.find(id) != actions_.end()) {
            return false;
        }
        actions_.emplace(
            id,
            Entry{
                std::move(descriptor),
                std::move(handler),
            });
    }

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
    std::string key;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = actions_.find(id);
        if (it == actions_.end()) {
            return false;
        }
        key = it->first;
        actions_.erase(it);
    }

    if (events_ != nullptr) {
        events_->publish({"action.removed", {{"id", key}}});
    }
    return true;
}

std::optional<ActionDescriptor> ActionRegistry::describe(
    std::string_view id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = actions_.find(id);
    if (it == actions_.end()) {
        return std::nullopt;
    }
    return it->second.descriptor;
}

std::vector<ActionDescriptor> ActionRegistry::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ActionDescriptor> result;
    result.reserve(actions_.size());
    for (const auto& [id, entry] : actions_) {
        (void)id;
        result.push_back(entry.descriptor);
    }
    return result;
}

std::size_t ActionRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return actions_.size();
}

ActionResult ActionRegistry::invoke(std::string_view id,
                                    const ActionContext& context,
                                    const StateValues& arguments) const {
    ActionDescriptor descriptor;
    Handler handler;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = actions_.find(id);
        if (it != actions_.end()) {
            descriptor = it->second.descriptor;
            handler = it->second.handler;
        }
    }

    if (!handler) {
        const ActionResult result{false, "unknown action", {}};
        if (events_ != nullptr) {
            events_->publish({
                "action.invoked",
                {
                    {"id", std::string(id)},
                    {"actor", context.actor},
                    {"capability", ""},
                    {"ok", "false"},
                    {"message", result.message},
                },
            });
        }
        return result;
    }

    ActionResult result;
    if (!context.has_capability(descriptor.capability)) {
        result = {
            false,
            "capability denied: " + descriptor.capability,
            {},
        };
    } else {
        result = handler(arguments);
    }

    if (events_ != nullptr) {
        events_->publish({
            "action.invoked",
            {
                {"id", descriptor.id},
                {"actor", context.actor},
                {"capability", descriptor.capability},
                {"ok", result.ok ? "true" : "false"},
                {"message", result.message},
            },
        });
    }
    return result;
}

}  // namespace deos
