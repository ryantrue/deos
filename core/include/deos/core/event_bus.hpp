#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace deos {

struct Event {
    std::string type;
    std::map<std::string, std::string> data;
};

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    void subscribe(std::string type, Handler handler);
    void publish(const Event& event) const;

private:
    std::map<std::string, std::vector<Handler>> handlers_;
};

}  // namespace deos
