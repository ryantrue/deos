// SPDX-License-Identifier: Apache-2.0

#include "deos/core/state.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace deos {

namespace {

std::string value_type_name(StateValueType type) {
    switch (type) {
        case StateValueType::Boolean: return "boolean";
        case StateValueType::Integer: return "integer";
        case StateValueType::Number: return "number";
        case StateValueType::String: return "string";
    }
    return "unknown";
}

}  // namespace

StateValueType state_value_type(const StateValue& value) noexcept {
    switch (value.index()) {
        case 0: return StateValueType::Boolean;
        case 1: return StateValueType::Integer;
        case 2: return StateValueType::Number;
        default: return StateValueType::String;
    }
}

std::string to_string(const StateValue& value) {
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        std::ostringstream out;
        out << std::setprecision(12) << *number;
        return out.str();
    }
    return std::get<std::string>(value);
}

EntityRegistry::EntityRegistry(EventBus* events) : events_(events) {}

bool EntityRegistry::register_entity(EntityDescriptor descriptor,
                                     StateValue initial_value) {
    if (descriptor.id.empty()) {
        return false;
    }
    if (descriptor.name.empty()) {
        descriptor.name = descriptor.id;
    }

    const std::string id = descriptor.id;
    const StateValueType type = state_value_type(initial_value);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entities_.find(id) != entities_.end()) {
            return false;
        }
        entities_.emplace(
            id,
            EntitySnapshot{
                std::move(descriptor),
                std::move(initial_value),
                1,
            });
    }

    if (events_ != nullptr) {
        events_->publish({
            "state.registered",
            {
                {"id", id},
                {"type", value_type_name(type)},
            },
        });
    }
    return true;
}

bool EntityRegistry::unregister_entity(std::string_view id) {
    std::string key;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entities_.find(id);
        if (it == entities_.end()) {
            return false;
        }
        key = it->first;
        entities_.erase(it);
    }

    if (events_ != nullptr) {
        events_->publish({"state.removed", {{"id", key}}});
    }
    return true;
}

bool EntityRegistry::set(std::string_view id, StateValue value) {
    std::optional<Event> event;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entities_.find(id);
        if (it == entities_.end()) {
            return false;
        }

        if (state_value_type(it->second.value) != state_value_type(value)) {
            return false;
        }

        if (it->second.value == value) {
            return true;
        }

        const std::string before = to_string(it->second.value);
        const std::string after = to_string(value);
        it->second.value = std::move(value);
        ++it->second.revision;

        event = Event{
            "state.changed",
            {
                {"id", it->first},
                {"before", before},
                {"value", after},
                {"revision", std::to_string(it->second.revision)},
            },
        };
    }

    if (events_ != nullptr && event.has_value()) {
        events_->publish(*event);
    }
    return true;
}

std::optional<EntitySnapshot> EntityRegistry::get(std::string_view id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<EntitySnapshot> EntityRegistry::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EntitySnapshot> result;
    result.reserve(entities_.size());
    for (const auto& [id, snapshot] : entities_) {
        (void)id;
        result.push_back(snapshot);
    }
    return result;
}

std::size_t EntityRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities_.size();
}

}  // namespace deos
