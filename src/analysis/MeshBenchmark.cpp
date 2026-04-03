#include "analysis/MeshBenchmark.h"

#include <cmath>

#include <glm/geometric.hpp>

#include "geometry/Mesh.h"

namespace physisim::analysis {

nlohmann::json benchmarkMeshPair(const geometry::Mesh& original, const geometry::Mesh& candidate,
                                 const MeshMetricsOptions& opt) {
    nlohmann::json j;
    j["schema"] = "physisim_mesh_benchmark_v1";

    auto a = computeMeshMetrics(original, opt);
    auto b = computeMeshMetrics(candidate, opt);

    j["original_metrics"] = a.json;
    j["candidate_metrics"] = b.json;

    float dv = b.signedVolumeMeshUnits - a.signedVolumeMeshUnits;
    float dabs = std::abs(b.signedVolumeMeshUnits) - std::abs(a.signedVolumeMeshUnits);
    j["delta_signed_volume_mesh_units"] = dv;
    j["delta_abs_volume_mesh_units"] = dabs;

    glm::vec3 dcom = b.centerOfMassMesh - a.centerOfMassMesh;
    float comShiftMesh = glm::length(dcom);
    j["delta_center_of_mass_mesh"] = {dcom.x, dcom.y, dcom.z};
    j["center_of_mass_shift_mesh_units"] = comShiftMesh;
    float mm = opt.meshUnitMeters > 0.f ? opt.meshUnitMeters * 1000.f : 1.f;
    j["center_of_mass_shift_mm"] = comShiftMesh * mm;

    j["delta_surface_area_mesh_units"] = b.surfaceAreaMeshUnits - a.surfaceAreaMeshUnits;

    if (a.json.contains("mass_kg") && a.json["mass_kg"].is_number() && b.json.contains("mass_kg") &&
        b.json["mass_kg"].is_number()) {
        float ma = a.json["mass_kg"].get<float>();
        float mb = b.json["mass_kg"].get<float>();
        j["delta_mass_kg"] = mb - ma;
        if (std::abs(ma) > 1e-20f) j["candidate_mass_ratio"] = mb / ma;
    }

    j["laplacian_proxy"] = nlohmann::json::object();
    j["laplacian_proxy"]["delta_vertex_rms"] = b.vertexLaplacianRms - a.vertexLaplacianRms;
    j["laplacian_proxy"]["note"] =
        "Difference in geometry-only Laplacian RMS; useful as a relative smoothness / bending-energy style "
        "indicator between two meshes—not beam deflection.";

    float charLen = std::max(a.json.value("bounds_diagonal_mesh_units", 1.f),
                             b.json.value("bounds_diagonal_mesh_units", 1.f));
    float complianceHint = std::abs(b.vertexLaplacianRms - a.vertexLaplacianRms) * charLen * charLen * charLen;
    j["deflection_proxy_arbitrary"] = complianceHint;
    j["deflection_proxy_note"] =
        "Arbitrary scale: |Δ Laplacian RMS| × diagonal³. Compare runs on the same part only; not calibrated to "
        "material or loads.";

    return j;
}

} // namespace physisim::analysis
