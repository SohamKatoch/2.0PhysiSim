#pragma once

#include <cstdint>
#include <vector>

#include "analysis/TriangleWeakness.h"

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::analysis {

/// Undirected triangle adjacency (triangles sharing an edge).
void buildTriangleNeighborGraph(const geometry::Mesh& mesh, std::vector<std::vector<uint32_t>>& outNeighbors);

/// One relaxation step: out[t] = max(in[t], max_neighbor(in[n]) * propagationFactor).
void propagateWeaknessStep(const std::vector<float>& inWeakness, const std::vector<std::vector<uint32_t>>& neighbors,
                           float propagationFactor, std::vector<float>& outWeakness);

/// Repeated steps for a simple temporal / growth preview.
void propagateWeaknessIterations(const std::vector<float>& seedWeakness,
                                 const std::vector<std::vector<uint32_t>>& neighbors, float propagationFactor,
                                 int iterations, std::vector<float>& outWeakness);

/// Scenario knobs: speed / longitudinal accel / cornering → velocityWeight and loadWeight proxies.
void applyKinematicWeaknessProxies(std::vector<TriangleWeakness>& tri, float speed01, float accelLong01,
                                   float cornering01);

} // namespace physisim::analysis
