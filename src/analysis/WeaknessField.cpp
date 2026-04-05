#include "analysis/WeaknessField.h"

#include <map>
#include <tuple>

#include <glm/geometric.hpp>

#include "geometry/Mesh.h"

namespace physisim::analysis {

namespace {

using EdgeKey = std::tuple<uint32_t, uint32_t>;

EdgeKey undirectedEdge(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return {a, b};
}

} // namespace

void buildTriangleNeighborGraph(const geometry::Mesh& mesh, std::vector<std::vector<uint32_t>>& outNeighbors) {
    outNeighbors.clear();
    const size_t triCount = mesh.indices.size() / 3;
    outNeighbors.assign(triCount, {});

    std::map<EdgeKey, std::vector<uint32_t>> edgeToTris;
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size()) continue;
        edgeToTris[undirectedEdge(ia, ib)].push_back(static_cast<uint32_t>(t));
        edgeToTris[undirectedEdge(ib, ic)].push_back(static_cast<uint32_t>(t));
        edgeToTris[undirectedEdge(ic, ia)].push_back(static_cast<uint32_t>(t));
    }

    for (const auto& kv : edgeToTris) {
        const std::vector<uint32_t>& tris = kv.second;
        if (tris.size() < 2) continue;
        for (size_t i = 0; i < tris.size(); ++i)
            for (size_t j = i + 1; j < tris.size(); ++j) {
                uint32_t a = tris[i], b = tris[j];
                outNeighbors[a].push_back(b);
                outNeighbors[b].push_back(a);
            }
    }
}

void propagateWeaknessStep(const std::vector<float>& inWeakness, const std::vector<std::vector<uint32_t>>& neighbors,
                           float propagationFactor, std::vector<float>& outWeakness) {
    const size_t n = inWeakness.size();
    outWeakness.resize(n);
    propagationFactor = std::clamp(propagationFactor, 0.f, 1.f);
    for (size_t t = 0; t < n; ++t) {
        float mxNbr = 0.f;
        if (t < neighbors.size()) {
            for (uint32_t nb : neighbors[t]) {
                if (nb < inWeakness.size()) mxNbr = std::max(mxNbr, inWeakness[nb]);
            }
        }
        float spread = mxNbr * propagationFactor;
        outWeakness[t] = std::clamp(std::max(inWeakness[t], spread), 0.f, 1.f);
    }
}

void propagateWeaknessIterations(const std::vector<float>& seedWeakness,
                               const std::vector<std::vector<uint32_t>>& neighbors, float propagationFactor,
                               int iterations, std::vector<float>& outWeakness) {
    if (seedWeakness.empty()) {
        outWeakness.clear();
        return;
    }
    std::vector<float> a = seedWeakness;
    std::vector<float> b;
    iterations = std::max(0, iterations);
    for (int i = 0; i < iterations; ++i) {
        propagateWeaknessStep(a, neighbors, propagationFactor, b);
        a.swap(b);
    }
    outWeakness = std::move(a);
}

void applyKinematicWeaknessProxies(std::vector<TriangleWeakness>& tri, float speed01, float accelLong01,
                                   float cornering01) {
    speed01 = std::clamp(speed01, 0.f, 1.f);
    accelLong01 = std::clamp(accelLong01, 0.f, 1.f);
    cornering01 = std::clamp(cornering01, 0.f, 1.f);
    for (auto& w : tri) {
        float baseMotion = std::clamp(0.55f * w.geoWeakness + 0.45f * w.stressProxy, 0.f, 1.f);
        w.velocityWeight = std::clamp(speed01 * baseMotion, 0.f, 1.f);
        float loadDrive = std::clamp(0.5f * accelLong01 + 0.5f * cornering01, 0.f, 1.f);
        w.loadWeight = std::clamp(loadDrive * (0.4f + 0.6f * w.stressProxy), 0.f, 1.f);
    }
}

} // namespace physisim::analysis
