#include "analysis/HeuristicAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

#include <glm/geometric.hpp>

#include "geometry/Mesh.h"

namespace physisim::analysis {

static std::tuple<glm::vec3, glm::vec3> bounds(const geometry::Mesh& m) {
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    for (const auto& p : m.positions) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    return {mn, mx};
}

nlohmann::json HeuristicAnalyzer::run(const geometry::Mesh& mesh) {
    nlohmann::json j;
    auto [mn, mx] = bounds(mesh);
    glm::vec3 ext = mx - mn;
    j["bounds_min"] = {mn.x, mn.y, mn.z};
    j["bounds_max"] = {mx.x, mx.y, mx.z};
    j["extent"] = {ext.x, ext.y, ext.z};
    j["triangle_count"] = mesh.indices.size() / 3;
    j["vertex_count"] = mesh.positions.size();

    float minEdge = std::numeric_limits<float>::max();
    float maxEdge = 0.f;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size())
            continue;
        glm::vec3 a = mesh.positions[ia], b = mesh.positions[ib], c = mesh.positions[ic];
        float e0 = glm::distance(a, b);
        float e1 = glm::distance(b, c);
        float e2 = glm::distance(c, a);
        minEdge = std::min({minEdge, e0, e1, e2});
        maxEdge = std::max({maxEdge, e0, e1, e2});
    }
    j["min_edge_length"] = minEdge;
    j["max_edge_length"] = maxEdge;
    if (maxEdge > 1e-8f) j["edge_length_ratio"] = minEdge / maxEdge;

    using EdgeKey = std::tuple<uint32_t, uint32_t>;
    std::map<EdgeKey, int> edgeCount;
    auto addEdge = [&](uint32_t u, uint32_t v) {
        if (u > v) std::swap(u, v);
        edgeCount[{u, v}]++;
    };
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        addEdge(ia, ib);
        addEdge(ib, ic);
        addEdge(ic, ia);
    }
    int nonManifold = 0;
    int boundary = 0;
    for (const auto& [_, c] : edgeCount) {
        if (c == 1) boundary++;
        if (c > 2) nonManifold++;
    }
    j["boundary_edge_count"] = boundary;
    j["non_manifold_edge_count"] = nonManifold;

    return j;
}

} // namespace physisim::analysis
