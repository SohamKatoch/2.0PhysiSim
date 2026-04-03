#pragma once

#include <cstdint>
#include <unordered_map>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace physisim::geometry {
struct Mesh;
}
namespace physisim::rendering {
class Camera;
}

namespace physisim::rendering {

/// Matches VulkanRenderer: Y-flipped projection, viewport origin top-left, `pixelY` downward.
void cameraViewportRay(const Camera& cam, float pixelX, float pixelY, float framebufferW, float framebufferH,
                       glm::vec3& outOriginWorld, glm::vec3& outDirWorld);

/// Closest hit along ray in world space. Triangle index = offset into `mesh.indices` / 3.
bool pickMeshTriangle(const glm::vec3& rayOriginWorld, const glm::vec3& rayDirWorld, const glm::mat4& modelWorld,
                      const geometry::Mesh& mesh, float& outT, uint32_t& outTriIndex);

/// Undirected edge (min,max) -> number of adjacent faces (clamped to 255).
void buildMeshEdgeFaceCounts(const geometry::Mesh& mesh, std::unordered_map<uint64_t, uint8_t>& out);

} // namespace physisim::rendering
