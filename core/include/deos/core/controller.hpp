#pragma once

#include "deos/core/resource.hpp"

#include <string_view>

namespace deos {

class Controller {
public:
    virtual ~Controller() = default;

    virtual bool supports(std::string_view kind) const = 0;
    virtual ResourceStatus reconcile(const Resource& desired,
                                     const AppliedResource* current) = 0;
    virtual ResourceStatus remove(const AppliedResource& current) = 0;
};

}  // namespace deos
