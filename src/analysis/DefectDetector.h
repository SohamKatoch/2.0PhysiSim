#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::analysis {

struct DefectDetectorOptions {
    /// Initial AI interpretation (Ollama).
    bool useAi{false};
    /// Phase 1: after first AI pass, send engine-vs-AI feedback for calibrated refinement (interpretive only).
    bool feedbackLoop{false};
    /// Phase 2: inject similar past cases into the first AI prompt.
    bool useRag{false};
    /// Phase 2: append this run to on-disk memory (for future retrieval).
    bool persistCase{false};
    /// Scale mesh units → mm for ground_truth display (engine still works in mesh units).
    float meshUnitToMm{1.f};
    /// For mass = ρ·V (SI). ≤ 0 skips mass_kg in metrics JSON.
    float materialDensityKgPerM3{7850.f};
    std::string memoryDirectory{"analysis_memory"};
    /// Label stored cases (e.g. active scene node id).
    std::string caseLabel{"model"};
};

struct DefectReport {
    nlohmann::json merged;
    /// Same length as triangle count; used for AI stress_hotspot overlay (not serialized to merged JSON).
    std::vector<float> triangleStressProxy;
    std::string aiRaw;
    std::string refinementRaw;
    std::string lastError;
};

class DefectDetector {
public:
    DefectReport evaluate(const geometry::Mesh& mesh, const DefectDetectorOptions& opts);

    void setAnalysisModelHost(const std::string& host, int port);
    void setAnalysisModelName(const std::string& model);

private:
    static nlohmann::json parseJsonObject(const std::string& text);

    std::string host_{"127.0.0.1"};
    int port_{11434};
    std::string model_{"llama3.1:8b"};
};

} // namespace physisim::analysis
