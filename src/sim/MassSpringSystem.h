#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "sim/SimMaterial.h"

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::analysis {
struct TriangleWeakness;
}

namespace physisim::sim {

struct Constraint;

struct SimVertex {
    glm::vec3 position{};
    glm::vec3 velocity{};
    glm::vec3 force{};
    float mass{1.f};
    /// Per-axis velocity locks after integration (1 = locked). (1,1,1) = fixed to rest pose.
    glm::vec3 lockedAxes{0.f};
};

struct EdgeSpring {
    int v0{0};
    int v1{0};
    float restLength{1.f};
    float stiffness{1.f};
};

struct MassSpringParams {
    SimMaterial material{};
    float baseStiffness{80.f};
    float damping{2.5f};
    float maxDisplacement{0.f};
    int substepsPerFrame{4};
};

/// CPU mass–spring network built from unique mesh edges (triangle soup).
/// Stiffness per edge scales with max incident triangle geoWeakness: softer where geometry is weaker.
class MassSpringSystem {
public:
    void clear();

    /// `meshUnitToMm`: mesh units to millimeters (typical STL mm → 1). Used for lumped mass from surface area.
    /// When `enableConstraints` is true, open-boundary and heuristic mount constraints are applied.
    bool build(const geometry::Mesh& mesh, const std::vector<analysis::TriangleWeakness>& triWeakness,
               const MassSpringParams& params, float meshUnitToMm, bool enableConstraints,
               const std::vector<Constraint>* extraConstraints = nullptr);

    bool valid() const { return !vertices_.empty() && !springs_.empty(); }

    void setParams(const MassSpringParams& p) { params_ = p; }
    const MassSpringParams& params() const { return params_; }

    /// Effective body acceleration in model space (same convention as scenario solver: +Z forward, +X lateral, +Y up).
    void setExternalAcceleration(const glm::vec3& accelerationModel);

    void step(float dt);

    void applyPositionsToMesh(geometry::Mesh& mesh) const;

    /// Per-triangle strain from edges: strain = |L-L0|/L0, stress = E*strain; viewport uses normalizedStress =
    /// clamp(|strain|/material.maxStrain,0,1) per edge, then max over triangle.
    void computeTriangleStrainStress01(const geometry::Mesh& mesh, std::vector<float>& outPerTriangle) const;

    const std::vector<glm::vec3>& restPositions() const { return restPositions_; }

    void restoreRestGeometryToMesh(geometry::Mesh& mesh);

private:
    static bool isFullLock(const glm::vec3& lockedAxes) {
        return lockedAxes.x > 0.5f && lockedAxes.y > 0.5f && lockedAxes.z > 0.5f;
    }

    void accumulateSpringForces();
    void integrateExplicitEuler(float dt);
    void clampDisplacements();

    std::vector<SimVertex> vertices_;
    std::vector<EdgeSpring> springs_;
    std::vector<glm::vec3> restPositions_;
    MassSpringParams params_{};
    glm::vec3 externalAccel_{0.f};
};

} // namespace physisim::sim
