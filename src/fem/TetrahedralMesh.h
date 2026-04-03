#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace physisim::fem {

/// Volume mesh for solid FEM (CalculiX C3D4). Node and tet indices are 0-based in-engine;
/// writers map to 1-based Abaqus-style .inp numbering.
struct TetrahedralMesh {
    std::vector<glm::vec3> nodes;
    /// Four corner node indices per tetrahedron.
    std::vector<std::array<uint32_t, 4>> tets;

    bool empty() const { return nodes.empty() || tets.empty(); }

    /// One valid tet using four corners of the unit cube used by `geometry::Mesh::createUnitCube()`.
    static TetrahedralMesh singleCornerTetFromUnitCube();
};

} // namespace physisim::fem
