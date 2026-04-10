#pragma once

#include <glm/vec3.hpp>
#include <vector>

namespace physisim::analysis {
struct TriangleWeakness;
}

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::sim {

/// Per-vertex axis locks: component 1 = zero that velocity component after integration; (1,1,1) = full mount.
struct Constraint {
    int vertexIndex{-1};
    glm::vec3 lockedAxes{0.f};
};

/// Marks vertices on open mesh boundaries (triangle soup).
void markOpenBoundaryVertices(const geometry::Mesh& mesh, std::vector<uint8_t>& outOnBoundary);

/// Boundary vertices fully locked; interior vertices adjacent to boundary with low incident geo → Z-only lock.
void suggestAutoMountConstraints(const geometry::Mesh& mesh,
                                 const std::vector<analysis::TriangleWeakness>& triWeakness,
                                 std::vector<Constraint>& outConstraints);

/// Merge constraints on the same vertex (per-axis max).
void mergeConstraints(std::vector<Constraint>& io);

} // namespace physisim::sim
