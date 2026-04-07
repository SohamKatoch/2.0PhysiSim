#pragma once

#include <algorithm>
#include <cmath>

#include <glm/vec3.hpp>

namespace physisim::analysis {

/// Per-triangle multi-factor weakness (deterministic + proxies + optional AI/visualization fields).
struct TriangleWeakness {
    float geoWeakness{0.f};
    /// Laplacian / heuristic proxy from analysis (deterministic).
    float stressProxy{0.f};
    /// Mass–spring edge strain mapped to [0,1]; visualization uses max with stressProxy in the stress channel.
    float strainStress{0.f};
    float velocityWeight{0.f};
    float loadWeight{0.f};
    glm::vec3 defectDirection{0.f};

    static float combined(const TriangleWeakness& tri, float stressScale, float velocityScale, float loadScale) {
        float s = std::clamp(
            tri.geoWeakness + tri.stressProxy * stressScale + tri.velocityWeight * velocityScale +
                tri.loadWeight * loadScale,
            0.f, 1.f);
        return s;
    }

    /// Emphasize triangles where several channels are simultaneously elevated (soft AND).
    static float multiObjectiveAlignment(const TriangleWeakness& tri, float stressScale, float velocityScale,
                                         float loadScale) {
        float g = std::clamp(tri.geoWeakness, 0.f, 1.f);
        float st = std::clamp(tri.stressProxy * stressScale, 0.f, 1.f);
        float v = std::clamp(tri.velocityWeight * velocityScale, 0.f, 1.f);
        float l = std::clamp(tri.loadWeight * loadScale, 0.f, 1.f);
        return std::clamp(g * 0.35f + st * 0.35f + v * 0.15f + l * 0.15f, 0.f, 1.f);
    }
};

} // namespace physisim::analysis
