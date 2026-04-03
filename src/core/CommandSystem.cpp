#include "core/CommandSystem.h"

#include <sstream>

namespace physisim::core {

static const char* actionToString(CommandAction a) {
    switch (a) {
        case CommandAction::Create: return "create";
        case CommandAction::Modify: return "modify";
        case CommandAction::Boolean: return "boolean";
        case CommandAction::Transform: return "transform";
        case CommandAction::Analyze: return "analyze";
        case CommandAction::AnalyzeFem: return "analyze_fem";
        default: return "unknown";
    }
}

static CommandAction actionFromString(const std::string& s) {
    if (s == "create") return CommandAction::Create;
    if (s == "modify") return CommandAction::Modify;
    if (s == "boolean") return CommandAction::Boolean;
    if (s == "transform") return CommandAction::Transform;
    if (s == "analyze") return CommandAction::Analyze;
    if (s == "analyze_fem") return CommandAction::AnalyzeFem;
    return CommandAction::Unknown;
}

nlohmann::json Command::toJson() const {
    nlohmann::json j;
    j["action"] = actionToString(action);
    j["operations"] = operations;
    j["parameters"] = parameters;
    if (target) j["target"] = *target;
    return j;
}

std::optional<Command> Command::fromJson(const nlohmann::json& j) {
    if (!j.contains("action") || !j["action"].is_string()) return std::nullopt;
    Command c;
    c.action = actionFromString(j["action"].get<std::string>());
    if (c.action == CommandAction::Unknown) return std::nullopt;
    if (j.contains("operations") && j["operations"].is_array())
        c.operations = j["operations"];
    else
        c.operations = nlohmann::json::array();
    if (j.contains("parameters") && j["parameters"].is_object())
        c.parameters = j["parameters"];
    else
        c.parameters = nlohmann::json::object();
    if (j.contains("target") && j["target"].is_string())
        c.target = j["target"].get<std::string>();
    return c;
}

bool CommandSystem::submit(const Command& cmd) {
    history_.push_back(cmd.toJson().dump());
    if (handler_) return handler_(cmd);
    return true;
}

bool CommandSystem::submitJson(const std::string& jsonText, std::string& errOut) {
    try {
        auto j = nlohmann::json::parse(jsonText);
        auto cmd = Command::fromJson(j);
        if (!cmd) {
            errOut = "Invalid command schema";
            return false;
        }
        return submit(*cmd);
    } catch (const std::exception& e) {
        errOut = e.what();
        return false;
    }
}

} // namespace physisim::core
