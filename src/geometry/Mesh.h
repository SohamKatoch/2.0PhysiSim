#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace physisim::geometry {

struct Mesh {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<uint32_t> indices;
    /// Per-vertex pack: (geo, stress proxy, velocity weight, load weight) in [0,1], averaged from incident triangles.
    std::vector<glm::vec4> defectHighlight;
    /// Neighbor-propagated weakness scalar [0,1] for temporal preview (5th vertex attribute).
    std::vector<float> weaknessPropagated;
    /// Hover / selection cursor highlight [0,1], uploaded as pick vertex channel (yellow emphasis in shader).
    std::vector<float> pickHighlight;

    void clear();
    void recomputeNormals();
    void ensureHighlightBuffer();
    void ensurePickBuffer();
    void clearPickHighlight();
    /// Sets pick weight on the three corners of triangle `triIndex` (index into mesh.indices / 3).
    void setTrianglePickWeight(uint32_t triIndex, float w01);

    static bool loadStl(const std::string& path, Mesh& out, std::string& err);
    static bool loadStlMemory(const void* data, size_t byteCount, Mesh& out, std::string& err);

    /// Binary STL (little-endian). Writes one facet per triangle; facet normals from geometry.
    bool writeStlBinary(std::ostream& os, std::string& err) const;
    bool saveStlBinary(const std::string& path, std::string& err) const;
    bool saveStlBinaryToVector(std::vector<uint8_t>& out, std::string& err) const;

    static Mesh createUnitCube();
};

} // namespace physisim::geometry
