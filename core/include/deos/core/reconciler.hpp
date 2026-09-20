#pragma once

#include "deos/core/controller.hpp"
#include "deos/core/plan.hpp"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace deos {

class Reconciler {
public:
    void register_controller(std::shared_ptr<Controller> controller);

    std::vector<PlanStep> plan(const ResourceMap& desired) const;
    void apply(ResourceMap desired);

    // Event-driven: work is only performed for queued resources.
    // Returns the number of reconciliation operations executed.
    std::size_t run_until_idle(std::size_t max_operations = 1024);

    const ResourceMap& desired() const noexcept { return desired_; }
    const AppliedResourceMap& actual() const noexcept { return actual_; }

private:
    Controller* controller_for(const ResourceKey& key) const;
    bool dependencies_ready(const Resource& resource) const;
    void enqueue(const ResourceKey& key);
    void enqueue_dependents(const ResourceKey& key);

    ResourceMap desired_;
    AppliedResourceMap actual_;
    std::vector<std::shared_ptr<Controller>> controllers_;
    std::deque<ResourceKey> queue_;
    std::set<ResourceKey> queued_;
    std::map<ResourceKey, std::vector<ResourceKey>> dependents_;
};

}  // namespace deos
