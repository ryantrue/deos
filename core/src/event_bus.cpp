#include "deos/core/event_bus.hpp"

namespace deos {

void EventBus::subscribe(std::string type, Handler handler) {
    handlers_[std::move(type)].push_back(std::move(handler));
}

void EventBus::publish(const Event& event) const {
    if (const auto it = handlers_.find(event.type); it != handlers_.end()) {
        for (const auto& handler : it->second) {
            handler(event);
        }
    }
}

}  // namespace deos
