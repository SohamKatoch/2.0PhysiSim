#include "ai/OptimizerCommands.h"

#include <sstream>

#include "ai/CommandValidator.h"

namespace physisim::ai {

namespace {

nlohmann::json extractJsonObject(const std::string& text) {
    auto start = text.find('{');
    auto end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start) return {};
    try {
        return nlohmann::json::parse(text.substr(start, end - start + 1));
    } catch (...) {
        return {};
    }
}

} // namespace

bool parseEngineProposals(const std::string& responseText, std::vector<EngineCommandProposal>& out, std::string& err) {
    out.clear();
    nlohmann::json root = extractJsonObject(responseText);
    if (root.is_null() || !root.is_object()) {
        err = "Model 2: no JSON object in response";
        return false;
    }
    if (!root.contains("proposals") || !root["proposals"].is_array()) {
        err = "Model 2: missing proposals array";
        return false;
    }
    for (const auto& p : root["proposals"]) {
        if (!p.is_object()) continue;
        EngineCommandProposal e;
        if (p.contains("confidence") && p["confidence"].is_number())
            e.confidence = p["confidence"].get<float>();
        if (p.contains("rationale") && p["rationale"].is_string()) e.rationale = p["rationale"].get<std::string>();
        if (p.contains("command") && p["command"].is_object()) e.command = p["command"];
        if (!e.command.empty()) out.push_back(std::move(e));
    }
    if (out.empty()) {
        err = "Model 2: proposals array empty or invalid";
        return false;
    }
    return true;
}

void filterValidateProposals(std::vector<EngineCommandProposal>& io, float minConfidence, size_t maxCount,
                             std::string& logOut) {
    std::ostringstream log;
    std::vector<EngineCommandProposal> kept;
    for (auto& p : io) {
        if (p.confidence < minConfidence) {
            log << "[model2] skip low confidence " << p.confidence << "\n";
            continue;
        }
        std::string verr;
        if (!CommandValidator::validateJson(p.command, verr)) {
            log << "[model2] reject invalid command: " << verr << "\n";
            continue;
        }
        kept.push_back(std::move(p));
        if (kept.size() >= maxCount) break;
    }
    io = std::move(kept);
    logOut = log.str();
}

} // namespace physisim::ai
