// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/event_bus.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace deos {

using StateValue = std::variant<bool, std::int64_t, double, std::string>;
using StateValues = std::map<std::string, StateValue>;

enum class StateValueType {
    Boolean,
    Integer,
    Number,
    String,
};

struct EntityDescriptor {
    std::string id;
    std::string name;
    std::string source;
    std::string unit;
};

struct EntitySnapshot {
    EntityDescriptor descriptor;
    StateValue value;
    std::uint64_t revision{0};
};

StateValueType state_value_type(const StateValue& value) noexcept;
std::string to_string(const StateValue& value);

class EntityRegistry final {
public:
    explicit EntityRegistry(EventBus* events = nullptr);

    bool register_entity(EntityDescriptor descriptor, StateValue initial_value);
    bool unregister_entity(std::string_view id);
    bool set(std::string_view id, StateValue value);

    std::optional<EntitySnapshot> get(std::string_view id) const;
    std::vector<EntitySnapshot> list() const;
    std::size_t size() const noexcept;

private:
    EventBus* events_{nullptr};
    std::map<std::string, EntitySnapshot, std::less<>> entities_;
};

}  // namespace deos
