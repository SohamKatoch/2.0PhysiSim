#include "analysis/MeshMetrics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

#include "fea/MeshAdjacency.h"
#include "geometry/Mesh.h"

namespace physisim::analysis {

namespace {

glm::vec3 triCross(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return glm::cross(b - a, c - a);
}

float triArea(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return 0.5f * glm::length(triCross(a, b, c));
}

} // namespace

MeshMetricsResult computeMeshMetrics(const geometry::Mesh& mesh, const MeshMetricsOptions& opt) {
    MeshMetricsResult out;
    nlohmann::json& j = out.json;

    const size_t triCount = mesh.indices.size() / 3;
    out.triangleStressProxy.assign(triCount, 0.f);

    if (mesh.positions.empty() || triCount == 0) {
        j["status"] = "empty";
        return out;
    }

    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(-std::numeric_limits<float>::max());
    for (const auto& p : mesh.positions) {
        bmin = glm::min(bmin, p);
        bmax = glm::max(bmax, p);
    }
    float diag = glm::length(bmax - bmin);
    if (diag < 1e-20f) diag = 1.f;

    double signedVol = 0.0;
    double areaSum = 0.0;
    glm::dvec3 volMoment(0.0); // sum of V_t * centroid_tet for tet OABC

    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size()) continue;
        glm::vec3 a = mesh.positions[ia], b = mesh.positions[ib], c = mesh.positions[ic];
        glm::vec3 cr = glm::cross(b, c);
        double vt = static_cast<double>(glm::dot(a, cr)) / 6.0; // signed volume tet O,a,b,c
        signedVol += vt;
        areaSum += static_cast<double>(triArea(a, b, c));
        glm::dvec3 cent = (glm::dvec3(a) + glm::dvec3(b) + glm::dvec3(c)) / 4.0;
        volMoment += cent * vt;
    }

    out.surfaceAreaMeshUnits = static_cast<float>(areaSum);
    out.signedVolumeMeshUnits = static_cast<float>(signedVol);
    float absVol = std::abs(static_cast<float>(signedVol));
    out.volumeReliable = absVol > 1e-12f * std::max(1.f, diag * diag * diag);

    if (out.volumeReliable && std::abs(signedVol) > 1e-30) {
        glm::dvec3 com = volMoment / signedVol;
        out.centerOfMassMesh = glm::vec3(static_cast<float>(com.x), static_cast<float>(com.y), static_cast<float>(com.z));
    } else {
        glm::dvec3 acc(0.0);
        double wsum = 0.0;
        for (size_t t = 0; t < triCount; ++t) {
            uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
            if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size()) continue;
            glm::vec3 a = mesh.positions[ia], b = mesh.positions[ib], c = mesh.positions[ic];
            double at = static_cast<double>(triArea(a, b, c));
            acc += (glm::dvec3(a) + glm::dvec3(b) + glm::dvec3(c)) / 3.0 * at;
            wsum += at;
        }
        if (wsum > 1e-30) {
            glm::dvec3 com = acc / wsum;
            out.centerOfMassMesh = glm::vec3(static_cast<float>(com.x), static_cast<float>(com.y), static_cast<float>(com.z));
        }
    }

    std::vector<uint32_t> neighOff, neighIdx;
    fea::buildUndirectedNeighborCsr(mesh, neighOff, neighIdx);

    std::vector<float> vMag(mesh.positions.size(), 0.f);
    double sumSq = 0.0;
    size_t counted = 0;
    for (size_t vi = 0; vi < mesh.positions.size(); ++vi) {
        uint32_t lo = neighOff[vi], hi = neighOff[vi + 1];
        if (lo >= hi) continue;
        glm::vec3 acc(0.f);
        uint32_t deg = hi - lo;
        for (uint32_t k = lo; k < hi; ++k) {
            uint32_t nj = neighIdx[k];
            if (nj < mesh.positions.size()) acc += mesh.positions[nj];
        }
        glm::vec3 centroid = acc / static_cast<float>(deg);
        float mag = glm::length(centroid - mesh.positions[vi]) / diag;
        vMag[vi] = mag;
        sumSq += static_cast<double>(mag) * static_cast<double>(mag);
        ++counted;
    }
    out.vertexLaplacianRms =
        counted > 0 ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(counted))) : 0.f;

    float triMax = 0.f;
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size()) continue;
        float m = std::max({vMag[ia], vMag[ib], vMag[ic]});
        out.triangleStressProxy[t] = std::clamp(m * 4.f, 0.f, 1.f);
        triMax = std::max(triMax, out.triangleStressProxy[t]);
    }

    j["surface_area_mesh_units"] = out.surfaceAreaMeshUnits;
    j["signed_volume_mesh_units"] = out.signedVolumeMeshUnits;
    j["volume_abs_mesh_units"] = absVol;
    j["volume_reliable_for_mass"] = out.volumeReliable;
    j["center_of_mass_mesh"] = {out.centerOfMassMesh.x, out.centerOfMassMesh.y, out.centerOfMassMesh.z};
    j["mesh_unit_meters"] = opt.meshUnitMeters;
    j["bounds_diagonal_mesh_units"] = diag;

    float volM3 = absVol;
    if (opt.meshUnitMeters > 0.f)
        volM3 *= opt.meshUnitMeters * opt.meshUnitMeters * opt.meshUnitMeters;

    if (opt.densityKgPerM3 > 0.f && out.volumeReliable) {
        float mass = volM3 * opt.densityKgPerM3;
        j["mass_kg"] = mass;
        j["material_density_kg_m3"] = opt.densityKgPerM3;
    } else {
        j["mass_kg"] = nullptr;
        if (opt.densityKgPerM3 > 0.f)
            j["material_density_kg_m3"] = opt.densityKgPerM3;
        else
            j["material_density_kg_m3"] = nullptr;
    }

    j["laplacian_stress_proxy"] = nlohmann::json::object();
    j["laplacian_stress_proxy"]["vertex_rms_normalized"] = out.vertexLaplacianRms;
    j["laplacian_stress_proxy"]["triangle_peak"] = triMax;
    j["laplacian_stress_proxy"]["note"] =
        "Normalized discrete Laplacian magnitude (geometry-only). Not a material stress tensor; use for relative "
        "hot-spot comparison before/after edits.";

    if (!out.volumeReliable)
        j["com_note"] = "Volume from signed triangle sum is near zero or degenerate; CoG falls back to area-weighted "
                        "triangle centroids (shell approximation).";
    else
        j["com_note"] = "Center of mass assumes uniform density in the enclosed volume (watertight winding).";

    return out;
}

} // namespace physisim::analysis
