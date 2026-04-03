#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace physisim::geometry {

struct Mesh {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<uint32_t> indices;
    /// Per-vertex weight in [0,1]: 0 = neutral, low = greener (stronger), high = redder (weaker / worse).
    std::vector<float> defectHighlight;
    /// Hover / selection cursor highlight [0,1], uploaded as 4th vertex channel (yellow emphasis in shader).
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
