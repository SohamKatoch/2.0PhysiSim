#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "ai/AnalysisClient.h"
#include "ai/LLMClient.h"
#include "ai/MathClient.h"
#include "core/CommandSystem.h"

namespace physisim::ai {

class AIOrchestrator {
public:
    AIOrchestrator();

    /// Natural language → validated command JSON string (not executed here).
    bool interpretUserIntent(const std::string& userText, std::string& commandJsonOut, std::string& err);

    LLMClient& llm() { return llm_; }
    MathClient& math() { return math_; }
    AnalysisClient& analysis() { return analysis_; }

private:
    static std::string extractJsonObject(const std::string& text);
    static bool hasSymbolicParameters(const nlohmann::json& params);
    bool resolveParameters(nlohmann::json& params, std::string& err);

    LLMClient llm_;
    MathClient math_;
    AnalysisClient analysis_;
};

} // namespace physisim::ai
