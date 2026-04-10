#include "sim/MassSpringSystem.h"

#include "sim/Constraints.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include <glm/geometric.hpp>

#include "analysis/TriangleWeakness.h"
#include "geometry/Mesh.h"

namespace physisim::sim {

namespace {

using EdgeKey = std::tuple<uint32_t, uint32_t>;

EdgeKey undirected(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return {a, b};
}

} // namespace

void MassSpringSystem::clear() {
    vertices_.clear();
    springs_.clear();
    restPositions_.clear();
    externalAccel_ = glm::vec3(0.f);
}

bool MassSpringSystem::build(const geometry::Mesh& mesh, const std::vector<analysis::TriangleWeakness>& triWeakness,
                             const MassSpringParams& params, float meshUnitToMm, bool enableConstraints,
                             const std::vector<Constraint>* extraConstraints) {
    clear();
    params_ = params;
    const size_t V = mesh.positions.size();
    if (V == 0 || mesh.indices.size() < 3 || mesh.indices.size() % 3 != 0) return false;

    const size_t triCount = mesh.indices.size() / 3;
    std::map<EdgeKey, float> edgeMaxGeo;
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        float g = 0.f;
        if (t < triWeakness.size()) g = std::clamp(triWeakness[t].geoWeakness, 0.f, 1.f);
        auto bump = [&](uint32_t a, uint32_t b) {
            EdgeKey k = undirected(a, b);
            float& m = edgeMaxGeo[k];
            m = std::max(m, g);
        };
        bump(ia, ib);
        bump(ib, ic);
        bump(ic, ia);
    }

    restPositions_.resize(V);
    vertices_.resize(V);
    for (size_t i = 0; i < V; ++i) {
        restPositions_[i] = mesh.positions[i];
        vertices_[i].position = mesh.positions[i];
        vertices_[i].velocity = glm::vec3(0.f);
        vertices_[i].force = glm::vec3(0.f);
        vertices_[i].lockedAxes = glm::vec3(0.f);
        vertices_[i].mass = 1.f;
    }

    const float mu = std::max(meshUnitToMm, 1e-3f);
    const float metersPerMeshUnit = mu * 1e-3f;
    const float rho = std::max(params_.material.densityKgM3, 1.f);
    constexpr float shellThicknessM = 0.001f;
    std::vector<double> massAcc(V, 0.0);

    auto triArea = [&](uint32_t ia, uint32_t ib, uint32_t ic) -> float {
        if (ia >= V || ib >= V || ic >= V) return 0.f;
        glm::vec3 a = mesh.positions[ia], b = mesh.positions[ib], c = mesh.positions[ic];
        return 0.5f * glm::length(glm::cross(b - a, c - a));
    };

    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        float aU2 = triArea(ia, ib, ic);
        double areaM2 = static_cast<double>(aU2) * static_cast<double>(metersPerMeshUnit) * static_cast<double>(metersPerMeshUnit);
        double mTri = areaM2 * static_cast<double>(shellThicknessM) * static_cast<double>(rho);
        if (mTri <= 0.0 || !std::isfinite(mTri)) continue;
        double share = mTri / 3.0;
        massAcc[ia] += share;
        massAcc[ib] += share;
        massAcc[ic] += share;
    }

    for (size_t i = 0; i < V; ++i) {
        float m = static_cast<float>(massAcc[i]);
        vertices_[i].mass = std::clamp(m, 1e-5f, 1e9f);
    }

    if (enableConstraints) {
        std::vector<Constraint> cons;
        suggestAutoMountConstraints(mesh, triWeakness, cons);
        if (extraConstraints && !extraConstraints->empty()) {
            cons.insert(cons.end(), extraConstraints->begin(), extraConstraints->end());
            mergeConstraints(cons);
        } else
            mergeConstraints(cons);
        for (const Constraint& c : cons) {
            if (c.vertexIndex < 0 || static_cast<size_t>(c.vertexIndex) >= V) continue;
            glm::vec3& L = vertices_[static_cast<size_t>(c.vertexIndex)].lockedAxes;
            L.x = std::max(L.x, std::clamp(c.lockedAxes.x, 0.f, 1.f));
            L.y = std::max(L.y, std::clamp(c.lockedAxes.y, 0.f, 1.f));
            L.z = std::max(L.z, std::clamp(c.lockedAxes.z, 0.f, 1.f));
        }
    }

    springs_.reserve(edgeMaxGeo.size());
    for (const auto& kv : edgeMaxGeo) {
        uint32_t a = std::get<0>(kv.first);
        uint32_t b = std::get<1>(kv.first);
        if (a >= V || b >= V) continue;
        float geo = std::clamp(kv.second, 0.f, 1.f);
        float k = params_.baseStiffness * (1.f - geo * 0.95f);
        k = std::max(k, params_.baseStiffness * 0.05f);
        glm::vec3 p0 = vertices_[a].position;
        glm::vec3 p1 = vertices_[b].position;
        float r = glm::distance(p0, p1);
        if (r < 1e-20f) continue;
        springs_.push_back({static_cast<int>(a), static_cast<int>(b), r, k});
    }

    return !springs_.empty();
}

void MassSpringSystem::setExternalAcceleration(const glm::vec3& accelerationModel) {
    externalAccel_ = accelerationModel;
}

void MassSpringSystem::accumulateSpringForces() {
    for (SimVertex& v : vertices_) v.force = glm::vec3(0.f);

    for (const EdgeSpring& sp : springs_) {
        if (sp.v0 < 0 || sp.v1 < 0 || static_cast<size_t>(sp.v0) >= vertices_.size() ||
            static_cast<size_t>(sp.v1) >= vertices_.size())
            continue;
        SimVertex& va = vertices_[static_cast<size_t>(sp.v0)];
        SimVertex& vb = vertices_[static_cast<size_t>(sp.v1)];
        glm::vec3 delta = vb.position - va.position;
        float len = glm::length(delta);
        if (len < 1e-20f) continue;
        glm::vec3 dir = delta / len;
        float ext = len - sp.restLength;
        float mag = sp.stiffness * ext;
        glm::vec3 f = dir * mag;
        if (!isFullLock(va.lockedAxes)) va.force += f;
        if (!isFullLock(vb.lockedAxes)) vb.force -= f;
    }

    for (size_t i = 0; i < vertices_.size(); ++i) {
        SimVertex& v = vertices_[i];
        if (isFullLock(v.lockedAxes)) continue;
        v.force += externalAccel_ * v.mass;
    }
}

void MassSpringSystem::integrateExplicitEuler(float dt) {
    const float damp = std::clamp(params_.damping, 0.f, 50.f);
    for (size_t i = 0; i < vertices_.size(); ++i) {
        SimVertex& v = vertices_[i];
        if (isFullLock(v.lockedAxes)) {
            v.velocity = glm::vec3(0.f);
            v.position = restPositions_[i];
            continue;
        }
        glm::vec3 a = v.force / std::max(v.mass, 1e-6f);
        v.velocity += a * dt;
        v.velocity *= std::exp(-damp * dt);
        if (v.lockedAxes.x > 0.5f) v.velocity.x = 0.f;
        if (v.lockedAxes.y > 0.5f) v.velocity.y = 0.f;
        if (v.lockedAxes.z > 0.5f) v.velocity.z = 0.f;
        v.position += v.velocity * dt;
    }
}

void MassSpringSystem::clampDisplacements() {
    float maxD = params_.maxDisplacement;
    if (maxD <= 0.f) return;
    for (size_t i = 0; i < vertices_.size(); ++i) {
        SimVertex& v = vertices_[i];
        if (isFullLock(v.lockedAxes)) continue;
        glm::vec3 d = v.position - restPositions_[i];
        float L = glm::length(d);
        if (L > maxD) v.position = restPositions_[i] + (d / L) * maxD;
    }
}

void MassSpringSystem::step(float dt) {
    if (!valid() || dt <= 0.f) return;
    int sub = std::max(1, params_.substepsPerFrame);
    float h = dt / static_cast<float>(sub);
    for (int s = 0; s < sub; ++s) {
        accumulateSpringForces();
        integrateExplicitEuler(h);
        clampDisplacements();
    }
}

void MassSpringSystem::applyPositionsToMesh(geometry::Mesh& mesh) const {
    if (mesh.positions.size() != vertices_.size()) return;
    for (size_t i = 0; i < vertices_.size(); ++i) mesh.positions[i] = vertices_[i].position;
}

void MassSpringSystem::restoreRestGeometryToMesh(geometry::Mesh& mesh) {
    if (mesh.positions.size() != restPositions_.size()) return;
    mesh.positions = restPositions_;
    for (size_t i = 0; i < vertices_.size(); ++i) {
        vertices_[i].position = restPositions_[i];
        vertices_[i].velocity = glm::vec3(0.f);
    }
}

void MassSpringSystem::computeTriangleStrainStress01(const geometry::Mesh& mesh,
                                                       std::vector<float>& outPerTriangle) const {
    outPerTriangle.clear();
    if (restPositions_.empty() || vertices_.size() != restPositions_.size()) return;
    const size_t V = restPositions_.size();
    const size_t triCount = mesh.indices.size() / 3;
    outPerTriangle.assign(triCount, 0.f);
    const float maxStrain = std::max(params_.material.maxStrain, 1e-9f);
    const float young = std::max(params_.material.youngsModulusPa, 1.f);

    auto edgeNormalized = [&](uint32_t a, uint32_t b) -> float {
        if (a >= V || b >= V) return 0.f;
        float r = glm::distance(restPositions_[a], restPositions_[b]);
        if (r < 1e-20f) return 0.f;
        float L = glm::distance(vertices_[a].position, vertices_[b].position);
        float strain = (L - r) / r;
        [[maybe_unused]] const float stressPa = young * strain;
        return std::clamp(std::abs(strain) / maxStrain, 0.f, 1.f);
    };

    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        float e0 = edgeNormalized(ia, ib);
        float e1 = edgeNormalized(ib, ic);
        float e2 = edgeNormalized(ic, ia);
        outPerTriangle[t] = std::max({e0, e1, e2});
    }
}

} // namespace physisim::sim
