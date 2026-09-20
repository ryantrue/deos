#include "deos/core/controller.hpp"
#include "deos/core/event_bus.hpp"
#include "deos/core/reconciler.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

namespace {

class TestController final : public deos::Controller {
public:
    explicit TestController(std::string kind) : kind_(std::move(kind)) {}

    bool supports(std::string_view kind) const override { return kind == kind_; }

    deos::ResourceStatus reconcile(const deos::Resource& desired,
                                   const deos::AppliedResource*) override {
        ++reconcile_count;
        return {deos::Phase::Ready, "ok", desired.spec};
    }

    deos::ResourceStatus remove(const deos::AppliedResource&) override {
        ++remove_count;
        return {deos::Phase::Ready, "removed", {}};
    }

    int reconcile_count{0};
    int remove_count{0};

private:
    std::string kind_;
};

void test_plan_and_idempotency() {
    deos::Reconciler engine;
    auto controller = std::make_shared<TestController>("Display");
    engine.register_controller(controller);

    deos::ResourceMap desired;
    desired[{"Display", "primary"}] = {
        {"Display", "primary"}, {{"brightness", "70"}}, {}};

    const auto first_plan = engine.plan(desired);
    assert(first_plan.size() == 1);
    assert(first_plan.front().op == deos::PlanOp::Create);

    engine.apply(desired);
    assert(engine.run_until_idle() == 1);
    assert(controller->reconcile_count == 1);
    assert(engine.actual().at({"Display", "primary"}).status.phase == deos::Phase::Ready);

    const auto second_plan = engine.plan(desired);
    assert(second_plan.empty());

    engine.apply(desired);
    assert(engine.run_until_idle() == 0);
    assert(controller->reconcile_count == 1);
}

void test_update_and_delete() {
    deos::Reconciler engine;
    auto controller = std::make_shared<TestController>("Display");
    engine.register_controller(controller);

    deos::ResourceMap desired;
    desired[{"Display", "primary"}] = {
        {"Display", "primary"}, {{"brightness", "50"}}, {}};
    engine.apply(desired);
    engine.run_until_idle();

    desired.at({"Display", "primary"}).spec["brightness"] = "80";
    const auto update_plan = engine.plan(desired);
    assert(update_plan.size() == 1);
    assert(update_plan.front().op == deos::PlanOp::Update);
    engine.apply(desired);
    engine.run_until_idle();
    assert(controller->reconcile_count == 2);

    deos::ResourceMap empty;
    const auto delete_plan = engine.plan(empty);
    assert(delete_plan.size() == 1);
    assert(delete_plan.front().op == deos::PlanOp::Delete);
    engine.apply(empty);
    engine.run_until_idle();
    assert(controller->remove_count == 1);
    assert(engine.actual().empty());
}

void test_dependencies_are_event_driven() {
    deos::Reconciler engine;
    auto network = std::make_shared<TestController>("Network");
    auto ai = std::make_shared<TestController>("AIProvider");
    engine.register_controller(network);
    engine.register_controller(ai);

    deos::ResourceMap desired;
    desired[{"AIProvider", "qwen"}] = {
        {"AIProvider", "qwen"}, {{"endpoint", "local"}}, {{"Network", "wifi"}}};
    desired[{"Network", "wifi"}] = {
        {"Network", "wifi"}, {{"profile", "home"}}, {}};

    engine.apply(desired);
    engine.run_until_idle();

    assert(network->reconcile_count == 1);
    assert(ai->reconcile_count == 1);
    assert(engine.actual().at({"AIProvider", "qwen"}).status.phase == deos::Phase::Ready);
}

void test_unknown_controller_is_visible_error() {
    deos::Reconciler engine;
    deos::ResourceMap desired;
    desired[{"Unknown", "x"}] = {{"Unknown", "x"}, {}, {}};
    engine.apply(desired);
    engine.run_until_idle();
    assert(engine.actual().at({"Unknown", "x"}).status.phase == deos::Phase::Error);
}

void test_hundred_resource_dependency_chain() {
    deos::Reconciler engine;
    auto controller = std::make_shared<TestController>("Node");
    engine.register_controller(controller);

    deos::ResourceMap desired;
    for (int i = 0; i < 100; ++i) {
        const auto name = std::to_string(i);
        std::vector<deos::ResourceKey> dependencies;
        if (i > 0) {
            dependencies.push_back({"Node", std::to_string(i - 1)});
        }
        desired[{"Node", name}] = {{"Node", name}, {{"index", name}}, dependencies};
    }

    engine.apply(desired);
    const auto operations = engine.run_until_idle(10000);
    assert(operations < 10000);
    assert(engine.actual().size() == 100);
    for (const auto& [key, resource] : engine.actual()) {
        (void)key;
        assert(resource.status.phase == deos::Phase::Ready);
    }
}

void test_event_bus() {
    deos::EventBus bus;
    int count = 0;
    bus.subscribe("sd.inserted", [&](const deos::Event& event) {
        assert(event.data.at("device") == "sd0");
        ++count;
    });
    bus.publish({"sd.inserted", {{"device", "sd0"}}});
    assert(count == 1);
}

}  // namespace

int main() {
    test_plan_and_idempotency();
    test_update_and_delete();
    test_dependencies_are_event_driven();
    test_unknown_controller_is_visible_error();
    test_hundred_resource_dependency_chain();
    test_event_bus();
    std::cout << "All DEOS core tests passed.\n";
    return 0;
}
