#include "ai/CommandValidator.h"

#include <set>

namespace physisim::ai {

static const std::set<std::string> kActions = {"create", "modify", "boolean", "transform", "analyze",
                                               "analyze_fem"};

bool CommandValidator::validateJson(const nlohmann::json& j, std::string& err) {
    if (!j.is_object()) {
        err = "Command must be a JSON object";
        return false;
    }
    if (!j.contains("action") || !j["action"].is_string()) {
        err = "Missing string field 'action'";
        return false;
    }
    std::string a = j["action"].get<std::string>();
    if (!kActions.count(a)) {
        err = "Invalid action";
        return false;
    }
    if (j.contains("operations") && !j["operations"].is_array()) {
        err = "'operations' must be an array";
        return false;
    }
    if (j.contains("parameters") && !j["parameters"].is_object()) {
        err = "'parameters' must be an object";
        return false;
    }
    if (j.contains("target") && !j["target"].is_string()) {
        err = "'target' must be a string";
        return false;
    }
    return true;
}

bool CommandValidator::validate(const core::Command& cmd, std::string& err) {
    if (cmd.action == core::CommandAction::Unknown) {
        err = "Unknown action enum";
        return false;
    }
    return validateJson(cmd.toJson(), err);
}

} // namespace physisim::ai
