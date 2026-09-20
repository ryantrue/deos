#pragma once

#include "deos/core/resource.hpp"

#include <map>
#include <vector>

namespace deos {

enum class PlanOp {
    Create,
    Update,
    Delete,
};

struct PlanStep {
    PlanOp op;
    ResourceKey key;
    std::optional<Resource> before;
    std::optional<Resource> after;
};

using ResourceMap = std::map<ResourceKey, Resource>;
using AppliedResourceMap = std::map<ResourceKey, AppliedResource>;

std::vector<PlanStep> build_plan(
    const AppliedResourceMap& actual,
    const ResourceMap& desired);

std::string to_string(PlanOp op);

}  // namespace deos
