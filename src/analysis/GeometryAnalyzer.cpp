#include "analysis/GeometryAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

#include <glm/geometric.hpp>

#include "geometry/Mesh.h"

namespace physisim::analysis {

namespace {

/// Map JSON severity 1–5 to weakness [0,1] (higher severity → redder in viewport).
float severityToWeakness(int sev) {
    return static_cast<float>(std::clamp(sev, 1, 5) - 1) / 4.f;
}

} // namespace

GeometryAnalysisResult GeometryAnalyzer::analyze(const geometry::Mesh& mesh) {
    GeometryAnalysisResult r;
    r.report = nlohmann::json::object();
    auto& rep = r.report;

    if (mesh.indices.empty() || mesh.positions.empty()) {
        rep["status"] = "empty";
        r.triWeaknessAll.clear();
        return r;
    }

    rep["deterministic_checks"] = nlohmann::json::array();

    float minEdge = std::numeric_limits<float>::max();
    float maxEdge = 0.f;
    const float thinRatioThreshold = 0.08f;

    using EdgeKey = std::tuple<uint32_t, uint32_t>;
    std::map<EdgeKey, int> edgeCount;

    auto addEdge = [&](uint32_t u, uint32_t v) {
        if (u > v) std::swap(u, v);
        edgeCount[{u, v}]++;
    };

    const size_t triCount = mesh.indices.size() / 3;
    r.triWeaknessAll.assign(triCount, 0.f);
    std::vector<float>& triW = r.triWeaknessAll;

    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size()) continue;
        glm::vec3 a = mesh.positions[ia], b = mesh.positions[ib], c = mesh.positions[ic];
        float e0 = glm::distance(a, b);
        float e1 = glm::distance(b, c);
        float e2 = glm::distance(c, a);
        float localMin = std::min({e0, e1, e2});
        float localMax = std::max({e0, e1, e2});
        minEdge = std::min(minEdge, localMin);
        maxEdge = std::max(maxEdge, localMax);
        if (localMax > 1e-12f) {
            float ratio = localMin / localMax;
            if (ratio < thinRatioThreshold) {
                float w = 1.f - ratio / thinRatioThreshold;
                triW[t] = std::max(triW[t], std::clamp(w, 0.f, 1.f));
            }
        }

        addEdge(ia, ib);
        addEdge(ib, ic);
        addEdge(ic, ia);
    }

    if (maxEdge > 1e-8f && (minEdge / maxEdge) < thinRatioThreshold) {
        nlohmann::json issue;
        issue["type"] = "thin_feature";
        issue["severity"] = 3;
        issue["detail"] = "Very short edges relative to longest edge — possible thin walls or slivers.";
        rep["deterministic_checks"].push_back(issue);
    }

    int nonManifold = 0;
    int boundary = 0;
    for (const auto& [_, c] : edgeCount) {
        if (c == 1) boundary++;
        if (c > 2) nonManifold++;
    }
    if (nonManifold > 0) {
        nlohmann::json issue;
        issue["type"] = "non_manifold_edges";
        issue["severity"] = 5;
        issue["detail"] = "Edges shared by more than two triangles.";
        rep["deterministic_checks"].push_back(issue);
    }
    if (boundary > 0) {
        nlohmann::json issue;
        issue["type"] = "open_boundary";
        issue["severity"] = 2;
        issue["detail"] = "Mesh has boundary edges (not watertight).";
        rep["deterministic_checks"].push_back(issue);
    }

    const float wNonManifold = severityToWeakness(5);
    const float wBoundary = severityToWeakness(2);

    if (nonManifold > 0 || boundary > 0) {
        for (size_t t = 0; t < triCount; ++t) {
            uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
            if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size()) continue;

            auto touchEdge = [&](uint32_t u, uint32_t v) {
                if (u > v) std::swap(u, v);
                auto it = edgeCount.find(EdgeKey{u, v});
                if (it == edgeCount.end()) return;
                int c = it->second;
                if (c > 2) triW[t] = std::max(triW[t], wNonManifold);
                if (c == 1) triW[t] = std::max(triW[t], wBoundary);
            };
            touchEdge(ia, ib);
            touchEdge(ib, ic);
            touchEdge(ic, ia);
        }
    }

    int badNormals = 0;
    std::vector<float> triNormalBad(triCount, 0.f);
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= mesh.normals.size() || ib >= mesh.normals.size() || ic >= mesh.normals.size()) continue;
        glm::vec3 a = mesh.positions[ia], b = mesh.positions[ib], c = mesh.positions[ic];
        glm::vec3 e1 = b - a, e2 = c - a;
        glm::vec3 fn = glm::normalize(glm::cross(e1, e2));
        if (!std::isfinite(fn.x)) continue;
        float align = glm::dot(fn, mesh.normals[ia]);
        if (align < -0.2f) {
            badNormals++;
            triNormalBad[t] = std::max(triNormalBad[t], -align);
        }
    }
    const float wBadNormal = severityToWeakness(3);
    if (badNormals > static_cast<int>(triCount) * 0.05f) {
        nlohmann::json issue;
        issue["type"] = "inverted_or_inconsistent_normals";
        issue["severity"] = 3;
        issue["detail"] = "Many face normals disagree with stored vertex normals.";
        rep["deterministic_checks"].push_back(issue);

        for (size_t t = 0; t < triCount; ++t) {
            if (triNormalBad[t] <= 0.f) continue;
            float inv = std::clamp((triNormalBad[t] - 0.2f) / (1.f - 0.2f), 0.f, 1.f);
            float w = wBadNormal * (0.35f + 0.65f * inv);
            triW[t] = std::max(triW[t], w);
        }
    }

    for (size_t t = 0; t < triCount; ++t) {
        if (triW[t] > 1e-5f) {
            r.highlightedTriangles.push_back(static_cast<uint32_t>(t));
            r.highlightedWeakness.push_back(std::clamp(triW[t], 0.f, 1.f));
        }
    }

    rep["highlight_triangle_count"] = r.highlightedTriangles.size();
    return r;
}

} // namespace physisim::analysis
