#include "deos/core/reconciler.hpp"

#include <stdexcept>

namespace deos {

void Reconciler::register_controller(std::shared_ptr<Controller> controller) {
    controllers_.push_back(std::move(controller));
}

std::vector<PlanStep> Reconciler::plan(const ResourceMap& desired) const {
    return build_plan(actual_, desired);
}

void Reconciler::enqueue(const ResourceKey& key) {
    if (queued_.insert(key).second) {
        queue_.push_back(key);
    }
}

void Reconciler::apply(ResourceMap desired) {
    const auto changes = build_plan(actual_, desired);
    desired_ = std::move(desired);
    dependents_.clear();
    for (const auto& [key, resource] : desired_) {
        for (const auto& dependency : resource.depends_on) {
            dependents_[dependency].push_back(key);
        }
    }

    for (const auto& step : changes) {
        enqueue(step.key);
    }
}

Controller* Reconciler::controller_for(const ResourceKey& key) const {
    for (const auto& controller : controllers_) {
        if (controller->supports(key.kind)) {
            return controller.get();
        }
    }
    return nullptr;
}

bool Reconciler::dependencies_ready(const Resource& resource) const {
    for (const auto& dependency : resource.depends_on) {
        const auto it = actual_.find(dependency);
        if (it == actual_.end() || it->second.status.phase != Phase::Ready) {
            return false;
        }
    }
    return true;
}

void Reconciler::enqueue_dependents(const ResourceKey& key) {
    if (const auto it = dependents_.find(key); it != dependents_.end()) {
        for (const auto& dependent : it->second) {
            enqueue(dependent);
        }
    }
}

std::size_t Reconciler::run_until_idle(std::size_t max_operations) {
    std::size_t operations = 0;

    while (!queue_.empty() && operations < max_operations) {
        const auto key = queue_.front();
        queue_.pop_front();
        queued_.erase(key);
        ++operations;

        Controller* controller = controller_for(key);
        if (controller == nullptr) {
            const auto desired_it = desired_.find(key);
            if (desired_it != desired_.end()) {
                actual_[key] = AppliedResource{
                    desired_it->second,
                    ResourceStatus{Phase::Error, "no controller registered for kind", {}}
                };
            }
            continue;
        }

        const auto desired_it = desired_.find(key);
        auto actual_it = actual_.find(key);

        if (desired_it == desired_.end()) {
            if (actual_it != actual_.end()) {
                const auto removal_status = controller->remove(actual_it->second);
                if (removal_status.phase == Phase::Ready) {
                    actual_.erase(actual_it);
                    enqueue_dependents(key);
                } else {
                    actual_it->second.status = removal_status;
                }
            }
            continue;
        }

        const Resource& wanted = desired_it->second;
        if (!dependencies_ready(wanted)) {
            AppliedResource waiting{wanted, ResourceStatus{Phase::Waiting, "waiting for dependencies", {}}};
            if (actual_it != actual_.end()) {
                waiting.status.observed = actual_it->second.status.observed;
            }
            actual_[key] = std::move(waiting);
            continue;
        }

        const AppliedResource* current = actual_it == actual_.end() ? nullptr : &actual_it->second;
        ResourceStatus status = controller->reconcile(wanted, current);
        actual_[key] = AppliedResource{wanted, std::move(status)};

        if (actual_[key].status.phase == Phase::Ready) {
            enqueue_dependents(key);
        }
    }

    return operations;
}

}  // namespace deos
