#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::analysis {

struct MeshMetricsOptions {
    /// Meters per one mesh unit (e.g. mm STL → 0.001).
    float meshUnitMeters{0.001f};
    /// Material density (kg/m³). If ≤ 0, mass fields are omitted.
    float densityKgPerM3{0.f};
};

struct MeshMetricsResult {
    nlohmann::json json;
    /// Per-triangle [0,1] Laplacian-magnitude proxy (max of vertex values on triangle).
    std::vector<float> triangleStressProxy;
    /// RMS of vertex Laplacian magnitudes (normalized by bbox diagonal).
    float vertexLaplacianRms{0.f};
    float surfaceAreaMeshUnits{0.f};
    float signedVolumeMeshUnits{0.f};
    glm::vec3 centerOfMassMesh{0.f};
    bool volumeReliable{false};
};

MeshMetricsResult computeMeshMetrics(const geometry::Mesh& mesh, const MeshMetricsOptions& opt);

} // namespace physisim::analysis
