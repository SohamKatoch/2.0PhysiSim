#include "analysis/SimulationInsightPack.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

#include "geometry/Mesh.h"

namespace physisim::analysis {

namespace {

glm::vec3 triCentroid(const geometry::Mesh& mesh, size_t tri) {
    size_t b = tri * 3;
    if (b + 2 >= mesh.indices.size()) return glm::vec3(0.f);
    uint32_t ia = mesh.indices[b], ib = mesh.indices[b + 1], ic = mesh.indices[b + 2];
    const size_t V = mesh.positions.size();
    if (ia >= V || ib >= V || ic >= V) return glm::vec3(0.f);
    return (mesh.positions[ia] + mesh.positions[ib] + mesh.positions[ic]) * (1.f / 3.f);
}

void meshBounds(const geometry::Mesh& mesh, glm::vec3& outMin, glm::vec3& outMax) {
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const auto& p : mesh.positions) {
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    }
}

std::string octantLabel(const glm::vec3& c, const glm::vec3& mn, const glm::vec3& mx) {
    glm::vec3 mid = (mn + mx) * 0.5f;
    std::string s;
    s += c.x >= mid.x ? 'p' : 'n';
    s += c.y >= mid.y ? 'p' : 'n';
    s += c.z >= mid.z ? 'p' : 'n';
    return "oct_" + s;
}

} // namespace

nlohmann::json buildModel2SimulationPack(const geometry::Mesh& mesh, const std::vector<float>& triStrain01,
                                           const std::vector<float>& triGeo01, float meshUnitToMm,
                                           const std::string& materialLabel, const std::string& scenarioLabel,
                                           bool constraintsEnabled, float thicknessMmEstimate) {
    const size_t triCount = mesh.indices.size() / 3;
    nlohmann::json j;
    j["schema"] = "physisim.model2.simulation_pack.v1";
    j["triangle_count"] = static_cast<int>(triCount);
    j["vertex_count"] = static_cast<int>(mesh.positions.size());
    j["mesh_unit_to_mm"] = meshUnitToMm;
    j["material"] = materialLabel;
    j["scenario"] = scenarioLabel;
    j["thickness_mm_estimate"] = thicknessMmEstimate;
    j["constraints_enabled"] = constraintsEnabled;
    nlohmann::json ctags = nlohmann::json::array();
    if (constraintsEnabled) {
        ctags.push_back("open_boundary_pins");
        ctags.push_back("heuristic_mount_partial_z");
    } else
        ctags.push_back("none");
    j["constraint_tags"] = std::move(ctags);

    if (triCount == 0) {
        j["metrics"] = {{"max_strain", 0.}, {"avg_strain", 0.}, {"p95_strain", 0.}};
        j["hotspots"] = nlohmann::json::array();
        return j;
    }

    std::vector<float> score(triCount, 0.f);
    for (size_t t = 0; t < triCount; ++t) {
        float s = 0.f;
        if (t < triStrain01.size()) s = std::max(s, std::clamp(triStrain01[t], 0.f, 1.f));
        if (t < triGeo01.size()) s = std::max(s, std::clamp(triGeo01[t], 0.f, 1.f) * 0.35f);
        score[t] = s;
    }

    double sum = 0.0;
    float mx = 0.f;
    for (float v : score) {
        sum += static_cast<double>(v);
        mx = std::max(mx, v);
    }
    float avg = static_cast<float>(sum / static_cast<double>(triCount));
    std::vector<float> sorted = score;
    const size_t idx95 = std::min(sorted.size() - 1, static_cast<size_t>(std::floor(0.95 * static_cast<double>(sorted.size() - 1))));
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(idx95), sorted.end());
    float p95 = sorted[idx95];

    nlohmann::json metrics;
    metrics["max_strain"] = mx;
    metrics["avg_strain"] = avg;
    metrics["p95_strain"] = p95;
    j["metrics"] = std::move(metrics);

    glm::vec3 bmin, bmax;
    meshBounds(mesh, bmin, bmax);

    std::vector<std::pair<float, size_t>> order;
    order.reserve(triCount);
    for (size_t t = 0; t < triCount; ++t) order.push_back({score[t], t});
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    const size_t kHot = std::min<size_t>(12, triCount);
    nlohmann::json hotspots = nlohmann::json::array();
    for (size_t i = 0; i < kHot; ++i) {
        size_t t = order[i].second;
        glm::vec3 c = triCentroid(mesh, t);
        nlohmann::json h;
        h["triangle_index"] = static_cast<int>(t);
        h["strain_score"] = order[i].first;
        h["region"] = octantLabel(c, bmin, bmax) + "_rank_" + std::to_string(i);
        h["centroid_mm_approx"] = nlohmann::json::array({c.x * meshUnitToMm, c.y * meshUnitToMm, c.z * meshUnitToMm});
        hotspots.push_back(std::move(h));
    }
    j["hotspots"] = std::move(hotspots);

    j["disclaimer"] =
        "All numeric fields are engine-computed summaries from mesh + simulation proxies. Model 2 must not "
        "invent or override them; propose PhysiSim command JSON only.";

    return j;
}

nlohmann::json fingerprintFromSimulationPack(const nlohmann::json& pack) {
    nlohmann::json f;
    int tri = pack.value("triangle_count", 0);
    f["log1p_triangles"] = std::log1p(static_cast<float>(std::max(0, tri)));
    f["non_manifold"] = 0.f;
    f["open_boundary"] = 0.f;
    f["thin"] = 0.f;
    f["edge_length_ratio"] = 1.f;
    if (pack.contains("metrics")) {
        const auto& m = pack["metrics"];
        f["max_strain"] = m.value("max_strain", 0.f);
        f["avg_strain"] = m.value("avg_strain", 0.f);
    } else {
        f["max_strain"] = 0.f;
        f["avg_strain"] = 0.f;
    }
    return f;
}

} // namespace physisim::analysis
