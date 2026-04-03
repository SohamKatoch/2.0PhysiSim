#include "rendering/RayPick.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "geometry/Mesh.h"
#include "rendering/Camera.h"

namespace physisim::rendering {

namespace {

uint64_t edgeKeyUndirected(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
}

bool rayTriangleMt(const glm::vec3& orig, const glm::vec3& dir, const glm::vec3& v0, const glm::vec3& v1,
                   const glm::vec3& v2, float& t) {
    const float eps = 1e-6f;
    glm::vec3 e1 = v1 - v0;
    glm::vec3 e2 = v2 - v0;
    glm::vec3 p = glm::cross(dir, e2);
    float det = glm::dot(e1, p);
    if (std::abs(det) < eps) return false;
    float invDet = 1.f / det;
    glm::vec3 tv = orig - v0;
    float u = glm::dot(tv, p) * invDet;
    if (u < 0.f || u > 1.f) return false;
    glm::vec3 q = glm::cross(tv, e1);
    float v = glm::dot(dir, q) * invDet;
    if (v < 0.f || u + v > 1.f) return false;
    float tt = glm::dot(e2, q) * invDet;
    if (tt < eps) return false;
    t = tt;
    return true;
}

} // namespace

void cameraViewportRay(const Camera& cam, float pixelX, float pixelY, float framebufferW, float framebufferH,
                       glm::vec3& outOriginWorld, glm::vec3& outDirWorld) {
    if (framebufferW <= 1.f || framebufferH <= 1.f) {
        outOriginWorld = cam.eyePosition();
        outDirWorld = glm::vec3(0.f, 0.f, -1.f);
        return;
    }
    glm::mat4 proj = cam.projMatrix();
    proj[1][1] *= -1.f;
    glm::mat4 invPV = glm::inverse(proj * cam.viewMatrix());
    float ndcX = (2.f * pixelX / framebufferW) - 1.f;
    float ndcY = 1.f - (2.f * pixelY / framebufferH);
    auto hom = [&](float zClip) {
        glm::vec4 c(ndcX, ndcY, zClip, 1.f);
        glm::vec4 w = invPV * c;
        return glm::vec3(w) / w.w;
    };
    glm::vec3 nearW = hom(0.f);
    glm::vec3 farW = hom(1.f);
    outOriginWorld = nearW;
    outDirWorld = glm::normalize(farW - nearW);
}

bool pickMeshTriangle(const glm::vec3& rayOriginWorld, const glm::vec3& rayDirWorld, const glm::mat4& modelWorld,
                      const geometry::Mesh& mesh, float& outT, uint32_t& outTriIndex) {
    glm::mat4 invM = glm::inverse(modelWorld);
    glm::vec3 ro = glm::vec3(invM * glm::vec4(rayOriginWorld, 1.f));
    glm::vec3 rd = glm::normalize(glm::vec3(invM * glm::vec4(rayDirWorld, 0.f)));

    float bestT = std::numeric_limits<float>::infinity();
    uint32_t bestTri = UINT32_MAX;
    const size_t triCount = mesh.indices.size() / 3;

    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size()) continue;
        const glm::vec3& a = mesh.positions[ia];
        const glm::vec3& b = mesh.positions[ib];
        const glm::vec3& c = mesh.positions[ic];
        glm::vec3 fn = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(fn, rd) >= 0.f) continue; // back-face relative to ray (matches back-face cull)

        float tt = 0.f;
        if (!rayTriangleMt(ro, rd, a, b, c, tt)) continue;
        if (tt < bestT) {
            bestT = tt;
            bestTri = static_cast<uint32_t>(t);
        }
    }

    if (bestTri == UINT32_MAX) return false;
    outT = bestT;
    outTriIndex = bestTri;
    return true;
}

void buildMeshEdgeFaceCounts(const geometry::Mesh& mesh, std::unordered_map<uint64_t, uint8_t>& out) {
    out.clear();
    const size_t triCount = mesh.indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        auto bump = [&](uint32_t u, uint32_t v) {
            uint64_t k = edgeKeyUndirected(u, v);
            uint8_t& c = out[k];
            if (c < 255) ++c;
        };
        bump(ia, ib);
        bump(ib, ic);
        bump(ic, ia);
    }
}

} // namespace physisim::rendering
