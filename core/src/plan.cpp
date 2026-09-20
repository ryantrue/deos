#include "deos/core/plan.hpp"

namespace deos {

std::vector<PlanStep> build_plan(
    const AppliedResourceMap& actual,
    const ResourceMap& desired) {

    std::vector<PlanStep> result;

    for (const auto& [key, wanted] : desired) {
        const auto current = actual.find(key);
        if (current == actual.end()) {
            result.push_back({PlanOp::Create, key, std::nullopt, wanted});
            continue;
        }

        const auto& applied = current->second.desired;
        if (applied.spec != wanted.spec || applied.depends_on != wanted.depends_on) {
            result.push_back({PlanOp::Update, key, applied, wanted});
        }
    }

    for (const auto& [key, current] : actual) {
        if (desired.find(key) == desired.end()) {
            result.push_back({PlanOp::Delete, key, current.desired, std::nullopt});
        }
    }

    return result;
}

std::string to_string(PlanOp op) {
    switch (op) {
        case PlanOp::Create: return "CREATE";
        case PlanOp::Update: return "UPDATE";
        case PlanOp::Delete: return "DELETE";
    }
    return "UNKNOWN";
}

}  // namespace deos
