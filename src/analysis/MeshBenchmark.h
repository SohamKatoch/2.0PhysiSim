#pragma once

#include <nlohmann/json.hpp>

#include "analysis/MeshMetrics.h"

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::analysis {

/// Deterministic A vs B report (volume, mass, CoG shift, Laplacian proxy). No AI.
nlohmann::json benchmarkMeshPair(const geometry::Mesh& original, const geometry::Mesh& candidate,
                                 const MeshMetricsOptions& opt);

} // namespace physisim::analysis
