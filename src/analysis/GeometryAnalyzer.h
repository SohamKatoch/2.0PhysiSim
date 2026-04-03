#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::analysis {

struct GeometryAnalysisResult {
    nlohmann::json report;
    /// Per-triangle weakness in [0,1], size = triangle count (deterministic basis for AI overlay merge).
    std::vector<float> triWeaknessAll;
    /// Triangle indices (into mesh index buffer /3) flagged for viewport emphasis.
    std::vector<uint32_t> highlightedTriangles;
    /// Parallel to `highlightedTriangles`: 0 = structurally strong (green tint), 1 = weak / worst (red tint).
    std::vector<float> highlightedWeakness;
};

class GeometryAnalyzer {
public:
    static GeometryAnalysisResult analyze(const geometry::Mesh& mesh);
};

} // namespace physisim::analysis
