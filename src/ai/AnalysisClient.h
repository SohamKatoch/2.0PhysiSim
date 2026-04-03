#pragma once

#include <string>

namespace physisim::ai {

struct AnalysisClientConfig {
    std::string host = "127.0.0.1";
    int port = 11434;
    /// Can be same as LLM or a dedicated instruct model.
    std::string model = "llama3.1:8b";
    int timeoutSeconds = 180;
};

class AnalysisClient {
public:
    explicit AnalysisClient(AnalysisClientConfig cfg = {});

    /// Pass mesh summary JSON; expect JSON text back describing defects (best-effort parse).
    /// Optional `ragContext` is prepended (Phase 2); does not override deterministic measurements.
    bool analyzeFeatures(const std::string& meshSummaryJson, std::string& outText, std::string& err,
                         const std::string& ragContext = {});

    /// Phase 1 context loop: AI refines interpretation given engine-vs-AI feedback. Must NOT restate
    /// engine numbers as overrides — suggestions and narrative only.
    bool refineWithFeedback(const std::string& feedbackPayloadJson, std::string& outText, std::string& err);

private:
    AnalysisClientConfig cfg_;
};

} // namespace physisim::ai
