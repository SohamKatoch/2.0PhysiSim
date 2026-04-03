#include "ai/AIOrchestrator.h"

#include <sstream>

#include "ai/CommandValidator.h"

namespace physisim::ai {

AIOrchestrator::AIOrchestrator() = default;

std::string AIOrchestrator::extractJsonObject(const std::string& text) {
    auto start = text.find('{');
    auto end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start) return {};
    return text.substr(start, end - start + 1);
}

bool AIOrchestrator::hasSymbolicParameters(const nlohmann::json& params) {
    if (!params.is_object()) return false;
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (it.value().is_string()) {
            std::string s = it.value().get<std::string>();
            if (s.find("expr:") == 0 || s.find("math:") == 0) return true;
        }
    }
    return false;
}

bool AIOrchestrator::resolveParameters(nlohmann::json& params, std::string& err) {
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (!it.value().is_string()) continue;
        std::string s = it.value().get<std::string>();
        if (s.rfind("math:", 0) == 0) {
            std::string q = s.substr(5);
            std::string ans;
            if (!math_.solve(q, ans, err)) return false;
            try {
                if (ans.find('.') != std::string::npos)
                    it.value() = std::stof(ans);
                else
                    it.value() = std::stoi(ans);
            } catch (...) {
                it.value() = ans;
            }
        }
    }
    return true;
}

bool AIOrchestrator::interpretUserIntent(const std::string& userText, std::string& commandJsonOut,
                                         std::string& err) {
    std::ostringstream sys;
    sys << "You output ONLY a single JSON object for a CAD command. Schema:\n"
        << "{\"action\":\"create|modify|boolean|transform|analyze\","
        << "\"operations\":[],\"parameters\":{},\"target\":optional string}\n"
        << "Examples:\n"
        << "- User: add a cube → {\"action\":\"create\",\"operations\":[],"
        << "\"parameters\":{\"primitive\":\"cube\",\"id\":\"main\"}}\n"
        << "- User: move up 1 unit → {\"action\":\"transform\",\"operations\":[],"
        << "\"parameters\":{\"translate\":[0,1,0]}}\n"
        << "For numeric values that need calculation, use strings like \"math:2^3+1\".\n"
        << "User request:\n"
        << userText;

    std::string raw;
    if (!llm_.generate(sys.str(), raw, err)) return false;

    std::string jsonStr = extractJsonObject(raw);
    if (jsonStr.empty()) {
        err = "LLM did not return JSON";
        return false;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonStr);
    } catch (const std::exception& e) {
        err = std::string("JSON parse: ") + e.what();
        return false;
    }

    if (j.contains("parameters") && hasSymbolicParameters(j["parameters"])) {
        if (!resolveParameters(j["parameters"], err)) return false;
    }

    std::string vErr;
    if (!CommandValidator::validateJson(j, vErr)) {
        err = vErr;
        return false;
    }

    auto cmd = core::Command::fromJson(j);
    if (!cmd) {
        err = "Command parse failed after validation";
        return false;
    }
    if (!CommandValidator::validate(*cmd, vErr)) {
        err = vErr;
        return false;
    }

    commandJsonOut = j.dump();
    return true;
}

} // namespace physisim::ai
