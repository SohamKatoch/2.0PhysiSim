#include "geometry/MeshOperations.h"

namespace physisim::geometry {

void MeshOperations::translate(Mesh& m, const glm::vec3& t) {
    for (auto& p : m.positions) p += t;
}

void MeshOperations::scaleUniform(Mesh& m, float s) {
    for (auto& p : m.positions) p *= s;
}

void MeshOperations::transform(Mesh& m, const glm::mat4& mat) {
    glm::mat3 nmat = glm::mat3(glm::transpose(glm::inverse(mat)));
    for (auto& p : m.positions) {
        glm::vec4 v = mat * glm::vec4(p, 1.f);
        p = glm::vec3(v);
    }
    for (auto& n : m.normals) n = glm::normalize(nmat * n);
}

} // namespace physisim::geometry
