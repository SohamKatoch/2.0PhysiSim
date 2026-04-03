#pragma once

#include <nlohmann/json.hpp>

namespace physisim::analysis {

/// Compares AI interpretation against engine ground truth. AI never mutates `ground_truth`.
nlohmann::json buildFeedbackPayload(const nlohmann::json& aiIssues, const nlohmann::json& groundTruth,
                                     const nlohmann::json& geometryDeterministicReport);

} // namespace physisim::analysis
