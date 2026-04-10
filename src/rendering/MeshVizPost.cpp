#include "rendering/MeshVizPost.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/common.hpp>

#include "geometry/Mesh.h"

namespace physisim::rendering {

void meshBuildVertexNeighbors(const geometry::Mesh& mesh, std::vector<std::vector<uint32_t>>& outNeighbors) {
    const size_t V = mesh.positions.size();
    outNeighbors.assign(V, {});
    const size_t triCount = mesh.indices.size() / 3;
    auto link = [&](uint32_t a, uint32_t b) {
        if (a >= V || b >= V || a == b) return;
        auto& na = outNeighbors[a];
        auto& nb = outNeighbors[b];
        if (std::find(na.begin(), na.end(), b) == na.end()) na.push_back(b);
        if (std::find(nb.begin(), nb.end(), a) == nb.end()) nb.push_back(a);
    };
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        link(ia, ib);
        link(ib, ic);
        link(ic, ia);
    }
}

void meshSmoothVertexVec4(const std::vector<std::vector<uint32_t>>& neighbors, int passes, float lambda,
                          std::vector<glm::vec4>& io) {
    if (io.empty() || passes <= 0) return;
    lambda = std::clamp(lambda, 0.f, 1.f);
    std::vector<glm::vec4> tmp(io.size());
    for (int p = 0; p < passes; ++p) {
        tmp = io;
        for (size_t i = 0; i < io.size(); ++i) {
            const auto& nb = neighbors[i];
            if (nb.empty()) continue;
            glm::vec4 sum(0.f);
            for (uint32_t j : nb) {
                if (j < tmp.size()) sum += tmp[j];
            }
            glm::vec4 avg = sum * (1.f / static_cast<float>(nb.size()));
            io[i] = glm::mix(tmp[i], avg, lambda);
        }
    }
}

void meshSmoothVertexScalars(const std::vector<std::vector<uint32_t>>& neighbors, int passes, float lambda,
                             std::vector<float>& io) {
    if (io.empty() || passes <= 0) return;
    lambda = std::clamp(lambda, 0.f, 1.f);
    std::vector<float> tmp(io.size());
    for (int p = 0; p < passes; ++p) {
        tmp = io;
        for (size_t i = 0; i < io.size(); ++i) {
            const auto& nb = neighbors[i];
            if (nb.empty()) continue;
            float sum = 0.f;
            for (uint32_t j : nb) {
                if (j < tmp.size()) sum += tmp[j];
            }
            float avg = sum / static_cast<float>(nb.size());
            io[i] = glm::mix(tmp[i], avg, lambda);
        }
    }
}

float meshVertexMixed(size_t vertexIndex, const geometry::Mesh& mesh, const MeshDefectViewParams& dv, float timeMix) {
    if (vertexIndex >= mesh.defectHighlight.size()) return 0.f;
    glm::vec4 dh = mesh.defectHighlight[vertexIndex];
    float r = std::clamp(dh.x, 0.f, 1.f);
    float g = std::clamp(dh.y, 0.f, 1.f);
    float b = std::clamp(dh.z, 0.f, 1.f);
    float a = std::clamp(dh.w, 0.f, 1.f);
    float sS = dv.stressScale;
    float sV = dv.velocityScale;
    float sL = dv.loadScale;
    float combined = std::clamp(r + g * sS + b * sV + a * sL, 0.f, 1.f);
    float prop = 0.f;
    if (vertexIndex < mesh.weaknessPropagated.size())
        prop = std::clamp(mesh.weaknessPropagated[vertexIndex], 0.f, 1.f);
    timeMix = std::clamp(timeMix, 0.f, 1.f);
    float mixed = glm::mix(combined, prop, timeMix);
    float align = std::clamp(r * 0.35f + g * sS * 0.35f + b * sV * 0.15f + a * sL * 0.15f, 0.f, 1.f);
    if (dv.visualMode > 1.5f) return align;
    return mixed;
}

void meshComputeMixedRange(const geometry::Mesh& mesh, const MeshDefectViewParams& dv, float timeMix, float& outMin,
                           float& outMax) {
    outMin = std::numeric_limits<float>::max();
    outMax = std::numeric_limits<float>::lowest();
    const size_t n = mesh.defectHighlight.size();
    if (n == 0) {
        outMin = 0.f;
        outMax = 1.f;
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        float v = meshVertexMixed(i, mesh, dv, timeMix);
        outMin = std::min(outMin, v);
        outMax = std::max(outMax, v);
    }
    if (!(outMax > outMin + 1e-8f)) {
        outMin = 0.f;
        outMax = 1.f;
    }
}

} // namespace physisim::rendering
