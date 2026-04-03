#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "core/CommandSystem.h"

namespace physisim::ai {

class CommandValidator {
public:
    /// Returns false and sets err with human-readable reason.
    static bool validate(const core::Command& cmd, std::string& err);
    static bool validateJson(const nlohmann::json& j, std::string& err);
};

} // namespace physisim::ai
