#pragma once

#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace deos {

struct ResourceKey {
    std::string kind;
    std::string name;

    bool operator<(const ResourceKey& other) const noexcept {
        return std::tie(kind, name) < std::tie(other.kind, other.name);
    }

    bool operator==(const ResourceKey& other) const noexcept {
        return kind == other.kind && name == other.name;
    }
};

using Fields = std::map<std::string, std::string>;

enum class Phase {
    Pending,
    Waiting,
    Ready,
    Error,
};

struct ResourceStatus {
    Phase phase{Phase::Pending};
    std::string message;
    Fields observed;
};

struct Resource {
    ResourceKey key;
    Fields spec;
    std::vector<ResourceKey> depends_on;
};

struct AppliedResource {
    Resource desired;
    ResourceStatus status;
};

std::string to_string(const ResourceKey& key);
std::string to_string(Phase phase);

}  // namespace deos
