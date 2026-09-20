#include "deos/core/controller.hpp"
#include "deos/core/reconciler.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

class GenericController final : public deos::Controller {
public:
    GenericController(std::string kind, std::string observed_key)
        : kind_(std::move(kind)), observed_key_(std::move(observed_key)) {}

    bool supports(std::string_view kind) const override {
        return kind == kind_;
    }

    deos::ResourceStatus reconcile(const deos::Resource& desired,
                                   const deos::AppliedResource*) override {
        deos::ResourceStatus status;
        status.phase = deos::Phase::Ready;
        status.message = "reconciled";
        if (const auto it = desired.spec.find(observed_key_); it != desired.spec.end()) {
            status.observed[observed_key_] = it->second;
        }
        return status;
    }

    deos::ResourceStatus remove(const deos::AppliedResource&) override {
        return {deos::Phase::Ready, "removed", {}};
    }

private:
    std::string kind_;
    std::string observed_key_;
};

void print_plan(const std::vector<deos::PlanStep>& plan) {
    if (plan.empty()) {
        std::cout << "No changes. System already matches desired state.\n";
        return;
    }

    for (const auto& step : plan) {
        std::cout << deos::to_string(step.op) << " " << deos::to_string(step.key) << "\n";
    }
}

}  // namespace

int main() {
    deos::Reconciler engine;
    engine.register_controller(std::make_shared<GenericController>("Network", "profile"));
    engine.register_controller(std::make_shared<GenericController>("AIProvider", "endpoint"));
    engine.register_controller(std::make_shared<GenericController>("Dashboard", "layout"));

    deos::ResourceMap desired;
    desired[{"Network", "wifi"}] = {
        {"Network", "wifi"},
        {{"profile", "home"}},
        {}
    };
    desired[{"AIProvider", "qwen"}] = {
        {"AIProvider", "qwen"},
        {{"endpoint", "http://192.168.1.20:8000/v1"}, {"model", "qwen"}},
        {{"Network", "wifi"}}
    };
    desired[{"Dashboard", "main"}] = {
        {"Dashboard", "main"},
        {{"layout", "2x2"}},
        {{"AIProvider", "qwen"}}
    };

    std::cout << "Plan #1\n";
    print_plan(engine.plan(desired));

    engine.apply(desired);
    const auto operations = engine.run_until_idle();
    std::cout << "\nApplied in " << operations << " reconciliation operations.\n\n";

    for (const auto& [key, resource] : engine.actual()) {
        std::cout << deos::to_string(key) << " => "
                  << deos::to_string(resource.status.phase)
                  << " (" << resource.status.message << ")\n";
    }

    std::cout << "\nPlan #2\n";
    print_plan(engine.plan(desired));
    return 0;
}
