#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::analysis {
struct TriangleWeakness;
}

namespace physisim::sim {

struct SimVertex {
    glm::vec3 position{};
    glm::vec3 velocity{};
    glm::vec3 force{};
    float mass{1.f};
    bool fixed{false};
};

struct EdgeSpring {
    int v0{0};
    int v1{0};
    float restLength{1.f};
    float stiffness{1.f};
};

struct MassSpringParams {
    float baseStiffness{80.f};
    float damping{2.5f};
    /// Max displacement from rest position per vertex (0 = disabled).
    float maxDisplacement{0.f};
    /// |L - L0| / L0 above this maps to stress 1.0 in triangle output.
    float strainReference{0.12f};
    float externalForceScale{12.f};
    int substepsPerFrame{4};
};

/// CPU mass–spring network built from unique mesh edges (triangle soup).
/// Stiffness per edge scales with max incident triangle geoWeakness: softer where geometry is weaker.
class MassSpringSystem {
public:
    void clear();

    /// Builds springs from `mesh` and stiffness from `triWeakness` (same triangle order as mesh.indices/3).
    /// Optionally pins vertices on open-boundary edges.
    bool build(const geometry::Mesh& mesh, const std::vector<analysis::TriangleWeakness>& triWeakness,
               const MassSpringParams& params, bool fixBoundaryVertices);

    bool valid() const { return !vertices_.empty() && !springs_.empty(); }

    void setParams(const MassSpringParams& p) { params_ = p; }
    const MassSpringParams& params() const { return params_; }

    /// Global inertial / load cues (0–1), mapped to model-space +Z forward, −Y vertical, +X lateral.
    void setExternalLoads01(float speed01, float accelLong01, float cornering01);

    void step(float dt);

    void applyPositionsToMesh(geometry::Mesh& mesh) const;

    /// Per-triangle max edge engineering strain magnitude, mapped to [0,1] using strainReference.
    void computeTriangleStrainStress01(const geometry::Mesh& mesh, std::vector<float>& outPerTriangle) const;

    const std::vector<glm::vec3>& restPositions() const { return restPositions_; }

    /// Copies rest positions into `mesh` and resets internal vertex state to match.
    void restoreRestGeometryToMesh(geometry::Mesh& mesh);

private:
    void accumulateSpringForces();
    void integrateExplicitEuler(float dt);
    void clampDisplacements();
    static void collectBoundaryVertices(const geometry::Mesh& mesh, std::vector<uint8_t>& outPinned);

    std::vector<SimVertex> vertices_;
    std::vector<EdgeSpring> springs_;
    std::vector<glm::vec3> restPositions_;
    MassSpringParams params_{};
    glm::vec3 externalAcceleration_{0.f};
};

} // namespace physisim::sim
