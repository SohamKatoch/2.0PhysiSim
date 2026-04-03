#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace physisim::analysis {

/// Phase 2: lightweight on-disk RAG — feature-tagged cases, no embedding model required yet.
class AnalysisMemory {
public:
    explicit AnalysisMemory(std::string storeDirectory = "analysis_memory");

    /// Feature vector for similarity (deterministic, comparable across runs).
    static nlohmann::json fingerprint(const nlohmann::json& heuristics, const nlohmann::json& groundTruth);

    /// Returns JSON array string of top similar past cases (for prompt injection). Never overwrites ground truth.
    std::string retrieveContextForPrompt(const nlohmann::json& fingerprint, size_t topK = 3) const;

    void saveCase(const std::string& modelLabel, const nlohmann::json& fingerprint,
                  const nlohmann::json& mergedReportSnapshot);

    const std::string& directory() const { return dir_; }

private:
    static float l1Distance(const nlohmann::json& a, const nlohmann::json& b);
    std::string dir_;
};

} // namespace physisim::analysis
