#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace physisim::core {

enum class CommandAction {
    Create,
    Modify,
    Boolean,
    Transform,
    Analyze,
    AnalyzeFem,
    Unknown
};

struct Command {
    CommandAction action{CommandAction::Unknown};
    nlohmann::json operations = nlohmann::json::array();
    nlohmann::json parameters = nlohmann::json::object();
    std::optional<std::string> target;

    nlohmann::json toJson() const;
    static std::optional<Command> fromJson(const nlohmann::json& j);
};

using CommandHandler = std::function<bool(const Command&)>;

class CommandSystem {
public:
    void setHandler(CommandHandler h) { handler_ = std::move(h); }

    bool submit(const Command& cmd);
    bool submitJson(const std::string& jsonText, std::string& errOut);

    const std::vector<std::string>& history() const { return history_; }

private:
    CommandHandler handler_;
    std::vector<std::string> history_;
};

} // namespace physisim::core
