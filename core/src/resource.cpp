#include "deos/core/resource.hpp"

namespace deos {

std::string to_string(const ResourceKey& key) {
    return key.kind + "/" + key.name;
}

std::string to_string(Phase phase) {
    switch (phase) {
        case Phase::Pending: return "Pending";
        case Phase::Waiting: return "Waiting";
        case Phase::Ready: return "Ready";
        case Phase::Error: return "Error";
    }
    return "Unknown";
}

}  // namespace deos
