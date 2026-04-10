#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace physisim::ai {

struct EngineCommandProposal {
    float confidence{0.f};
    std::string rationale;
    nlohmann::json command;
};

/// Extract `proposals` from Model 2 response text (best-effort JSON object).
bool parseEngineProposals(const std::string& responseText, std::vector<EngineCommandProposal>& out, std::string& err);

/// Keep at most `maxCount` entries with confidence >= minConfidence; validate each command JSON schema.
void filterValidateProposals(std::vector<EngineCommandProposal>& io, float minConfidence, size_t maxCount,
                             std::string& logOut);

} // namespace physisim::ai
