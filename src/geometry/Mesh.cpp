#include "geometry/Mesh.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <ostream>
#include <sstream>

namespace physisim::geometry {

void Mesh::clear() {
    positions.clear();
    normals.clear();
    indices.clear();
    defectHighlight.clear();
    weaknessPropagated.clear();
    pickHighlight.clear();
}

void Mesh::ensureHighlightBuffer() {
    defectHighlight.assign(positions.size(), glm::vec4(0.f));
    weaknessPropagated.assign(positions.size(), 0.f);
}

void Mesh::ensurePickBuffer() {
    pickHighlight.resize(positions.size(), 0.f);
}

void Mesh::clearPickHighlight() {
    std::fill(pickHighlight.begin(), pickHighlight.end(), 0.f);
}

void Mesh::setTrianglePickWeight(uint32_t triIndex, float w01) {
    w01 = std::clamp(w01, 0.f, 1.f);
    size_t base = static_cast<size_t>(triIndex) * 3;
    if (base + 2 >= indices.size()) return;
    for (int k = 0; k < 3; ++k) {
        uint32_t vi = indices[base + static_cast<size_t>(k)];
        if (vi < pickHighlight.size()) pickHighlight[vi] = std::max(pickHighlight[vi], w01);
    }
}

void Mesh::recomputeNormals() {
    normals.assign(positions.size(), glm::vec3(0.f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size()) continue;
        glm::vec3 e1 = positions[ib] - positions[ia];
        glm::vec3 e2 = positions[ic] - positions[ia];
        glm::vec3 n = glm::cross(e1, e2);
        float len = glm::length(n);
        if (len > 1e-20f) n /= len;
        normals[ia] += n;
        normals[ib] += n;
        normals[ic] += n;
    }
    for (auto& n : normals) {
        float len = glm::length(n);
        if (len > 1e-20f) n /= len;
        else
            n = glm::vec3(0.f, 1.f, 0.f);
    }
}

static bool readAsciiStl(std::istream& in, Mesh& out, std::string& err) {
    std::string line;
    std::getline(in, line);
    if (line.find("solid") == std::string::npos) {
        err = "Not ASCII STL (missing solid)";
        return false;
    }
    glm::vec3 fn{};
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tok;
        ls >> tok;
        if (tok == "facet") {
            std::string normalTok;
            ls >> normalTok;
            ls >> fn.x >> fn.y >> fn.z;
        } else if (tok == "vertex") {
            glm::vec3 v;
            ls >> v.x >> v.y >> v.z;
            uint32_t idx = static_cast<uint32_t>(out.positions.size());
            out.positions.push_back(v);
            out.indices.push_back(idx);
        }
    }
    if (out.indices.empty() || out.indices.size() % 3 != 0) {
        err = "ASCII STL parse failed";
        return false;
    }
    out.normals.resize(out.positions.size(), glm::vec3(0.f, 1.f, 0.f));
    out.recomputeNormals();
    out.ensureHighlightBuffer();
    out.ensurePickBuffer();
    return true;
}

static bool readBinaryStl(std::istream& in, Mesh& out, std::string& err) {
    in.seekg(0, std::ios::end);
    std::streamsize sz = in.tellg();
    in.seekg(0, std::ios::beg);
    if (sz < 84) {
        err = "Binary STL too small";
        return false;
    }
    char header[80];
    in.read(header, 80);
    uint32_t triCount = 0;
    in.read(reinterpret_cast<char*>(&triCount), 4);
    std::streamsize expected = 80 + 4 + static_cast<std::streamsize>(triCount) * 50;
    if (sz < expected) {
        err = "Binary STL triangle count mismatch";
        return false;
    }
    out.positions.reserve(out.positions.size() + triCount * 3);
    out.indices.reserve(out.indices.size() + triCount * 3);
    for (uint32_t t = 0; t < triCount; ++t) {
        float nx, ny, nz;
        in.read(reinterpret_cast<char*>(&nx), 4);
        in.read(reinterpret_cast<char*>(&ny), 4);
        in.read(reinterpret_cast<char*>(&nz), 4);
        (void)nx;
        (void)ny;
        (void)nz;
        for (int k = 0; k < 3; ++k) {
            float x, y, z;
            in.read(reinterpret_cast<char*>(&x), 4);
            in.read(reinterpret_cast<char*>(&y), 4);
            in.read(reinterpret_cast<char*>(&z), 4);
            uint32_t idx = static_cast<uint32_t>(out.positions.size());
            out.positions.emplace_back(x, y, z);
            out.indices.push_back(idx);
        }
        uint16_t attr;
        in.read(reinterpret_cast<char*>(&attr), 2);
    }
    out.recomputeNormals();
    out.ensureHighlightBuffer();
    out.ensurePickBuffer();
    return true;
}

bool Mesh::loadStl(const std::string& path, Mesh& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "Cannot open file";
        return false;
    }
    char head[5] = {0};
    f.read(head, 5);
    f.seekg(0, std::ios::beg);
    if (strncmp(head, "solid", 5) == 0) return readAsciiStl(f, out, err);
    return readBinaryStl(f, out, err);
}

bool Mesh::loadStlMemory(const void* data, size_t byteCount, Mesh& out, std::string& err) {
    out.clear();
    if (!data || byteCount < 84) {
        err = "STL buffer too small";
        return false;
    }
    const auto* bytes = static_cast<const char*>(data);
    std::string blob(bytes, bytes + byteCount);
    std::istringstream in(blob, std::ios::binary);
    if (byteCount >= 5 && std::strncmp(bytes, "solid", 5) == 0) return readAsciiStl(in, out, err);
    return readBinaryStl(in, out, err);
}

static void writeF(std::ostream& o, float v) { o.write(reinterpret_cast<const char*>(&v), 4); }

bool Mesh::writeStlBinary(std::ostream& os, std::string& err) const {
    if (indices.empty() || indices.size() % 3 != 0) {
        err = "Mesh has no valid triangles";
        return false;
    }
    char header[80] = {};
    const char* tag = "PHYSISIM binary STL; vertices in model units (use mm)";
    std::strncpy(header, tag, sizeof(header) - 1);
    os.write(header, 80);
    uint32_t ntri = static_cast<uint32_t>(indices.size() / 3);
    os.write(reinterpret_cast<const char*>(&ntri), 4);
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size()) {
            err = "Invalid vertex index";
            return false;
        }
        glm::vec3 a = positions[ia], b = positions[ib], c = positions[ic];
        glm::vec3 e1 = b - a, e2 = c - a;
        glm::vec3 n = glm::cross(e1, e2);
        float len = glm::length(n);
        if (len > 1e-20f)
            n /= len;
        else
            n = glm::vec3(0.f, 0.f, 1.f);
        writeF(os, n.x);
        writeF(os, n.y);
        writeF(os, n.z);
        writeF(os, a.x);
        writeF(os, a.y);
        writeF(os, a.z);
        writeF(os, b.x);
        writeF(os, b.y);
        writeF(os, b.z);
        writeF(os, c.x);
        writeF(os, c.y);
        writeF(os, c.z);
        uint16_t attr = 0;
        os.write(reinterpret_cast<const char*>(&attr), 2);
    }
    return true;
}

bool Mesh::saveStlBinary(const std::string& path, std::string& err) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        err = "Cannot open file for write";
        return false;
    }
    return writeStlBinary(f, err);
}

bool Mesh::saveStlBinaryToVector(std::vector<uint8_t>& out, std::string& err) const {
    std::ostringstream oss(std::ios::binary);
    if (!writeStlBinary(oss, err)) return false;
    std::string s = oss.str();
    out.assign(reinterpret_cast<const uint8_t*>(s.data()),
               reinterpret_cast<const uint8_t*>(s.data()) + s.size());
    return true;
}

Mesh Mesh::createUnitCube() {
    Mesh m;
    float h = 0.5f;
    m.positions = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h, h},  {h, -h, h},  {h, h, h},  {-h, h, h},
    };
    m.indices = {
        0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4, 0, 4, 7, 7, 3, 0, 1, 5, 6, 6, 2, 1,
        3, 2, 6, 6, 7, 3, 0, 1, 5, 5, 4, 0,
    };
    m.recomputeNormals();
    m.ensureHighlightBuffer();
    m.ensurePickBuffer();
    return m;
}

} // namespace physisim::geometry
