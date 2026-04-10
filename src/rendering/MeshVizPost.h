#pragma once

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "rendering/VulkanRenderer.h"

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::rendering {

/// Undirected vertex adjacency (for visualization smoothing only).
void meshBuildVertexNeighbors(const geometry::Mesh& mesh, std::vector<std::vector<uint32_t>>& outNeighbors);

void meshSmoothVertexVec4(const std::vector<std::vector<uint32_t>>& neighbors, int passes, float lambda,
                          std::vector<glm::vec4>& io);

void meshSmoothVertexScalars(const std::vector<std::vector<uint32_t>>& neighbors, int passes, float lambda,
                             std::vector<float>& io);

/// Same combination as `mesh.frag` uses for the primary heat scalar (before visual mode branches).
float meshVertexMixed(size_t vertexIndex, const geometry::Mesh& mesh, const MeshDefectViewParams& dv, float timeMix);

void meshComputeMixedRange(const geometry::Mesh& mesh, const MeshDefectViewParams& dv, float timeMix, float& outMin,
                           float& outMax);

} // namespace physisim::rendering
