#include "sim/Constraints.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "analysis/TriangleWeakness.h"
#include "geometry/Mesh.h"

namespace physisim::sim {

namespace {

using EdgeKey = std::tuple<uint32_t, uint32_t>;

EdgeKey undirected(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return {a, b};
}

} // namespace

void markOpenBoundaryVertices(const geometry::Mesh& mesh, std::vector<uint8_t>& outOnBoundary) {
    const size_t V = mesh.positions.size();
    outOnBoundary.assign(V, 0);
    std::map<EdgeKey, int> edgeFaceCount;
    const size_t triCount = mesh.indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        edgeFaceCount[undirected(ia, ib)]++;
        edgeFaceCount[undirected(ib, ic)]++;
        edgeFaceCount[undirected(ic, ia)]++;
    }
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        auto touch = [&](uint32_t u, uint32_t v) {
            auto it = edgeFaceCount.find(undirected(u, v));
            if (it != edgeFaceCount.end() && it->second == 1) {
                outOnBoundary[u] = 1;
                outOnBoundary[v] = 1;
            }
        };
        touch(ia, ib);
        touch(ib, ic);
        touch(ic, ia);
    }
}

void suggestAutoMountConstraints(const geometry::Mesh& mesh,
                                 const std::vector<analysis::TriangleWeakness>& triWeakness,
                                 std::vector<Constraint>& outConstraints) {
    outConstraints.clear();
    const size_t V = mesh.positions.size();
    if (V == 0) return;

    std::vector<uint8_t> onB;
    markOpenBoundaryVertices(mesh, onB);

    const size_t triCount = mesh.indices.size() / 3;
    std::vector<std::vector<uint32_t>> vertTris(V);
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia < V) vertTris[ia].push_back(static_cast<uint32_t>(t));
        if (ib < V) vertTris[ib].push_back(static_cast<uint32_t>(t));
        if (ic < V) vertTris[ic].push_back(static_cast<uint32_t>(t));
    }

    auto avgGeo = [&](uint32_t vi) -> float {
        if (vi >= vertTris.size() || vertTris[vi].empty()) return 1.f;
        float s = 0.f;
        int n = 0;
        for (uint32_t t : vertTris[vi]) {
            if (t < triWeakness.size()) {
                s += std::clamp(triWeakness[t].geoWeakness, 0.f, 1.f);
                ++n;
            }
        }
        return n > 0 ? s / static_cast<float>(n) : 1.f;
    };

    std::vector<uint8_t> nearB(V, 0);
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        uint32_t v[3] = {ia, ib, ic};
        for (int k = 0; k < 3; ++k) {
            if (onB[v[k]]) continue;
            uint32_t oa = v[(k + 1) % 3], ob = v[(k + 2) % 3];
            if (onB[oa] || onB[ob]) nearB[v[k]] = 1;
        }
    }

    for (size_t i = 0; i < V; ++i) {
        if (onB[i]) {
            outConstraints.push_back({static_cast<int>(i), glm::vec3(1.f)});
            continue;
        }
        if (nearB[i] && avgGeo(static_cast<uint32_t>(i)) < 0.35f)
            outConstraints.push_back({static_cast<int>(i), glm::vec3(0.f, 0.f, 1.f)});
    }
}

void mergeConstraints(std::vector<Constraint>& io) {
    std::map<int, glm::vec3> acc;
    for (const Constraint& c : io) {
        if (c.vertexIndex < 0) continue;
        glm::vec3& L = acc[c.vertexIndex];
        L.x = std::max(L.x, std::clamp(c.lockedAxes.x, 0.f, 1.f));
        L.y = std::max(L.y, std::clamp(c.lockedAxes.y, 0.f, 1.f));
        L.z = std::max(L.z, std::clamp(c.lockedAxes.z, 0.f, 1.f));
    }
    io.clear();
    for (const auto& kv : acc)
        io.push_back({kv.first, kv.second});
}

} // namespace physisim::sim
