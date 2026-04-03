#pragma once

#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "geometry/Mesh.h"

namespace physisim::geometry {

class MeshOperations {
public:
    static void translate(Mesh& m, const glm::vec3& t);
    static void scaleUniform(Mesh& m, float s);
    static void transform(Mesh& m, const glm::mat4& m4);
};

} // namespace physisim::geometry
