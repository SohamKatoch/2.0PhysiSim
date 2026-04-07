#include "sim/MassSpringSystem.h"

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
    externalAcceleration_ = glm::vec3(0.f);
}

void MassSpringSystem::collectBoundaryVertices(const geometry::Mesh& mesh, std::vector<uint8_t>& outPinned) {
    const size_t V = mesh.positions.size();
    outPinned.assign(V, 0);
    std::map<EdgeKey, int> edgeFaceCount;
    const size_t triCount = mesh.indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        edgeFaceCount[undirected(ia, ib)]++;
        edgeFaceCount[undirected(ib, ic)]++;
        edgeFaceCount[undirected(ic, ia)]++;
    }
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        auto touch = [&](uint32_t u, uint32_t v) {
            auto it = edgeFaceCount.find(undirected(u, v));
            if (it != edgeFaceCount.end() && it->second == 1) {
                outPinned[u] = 1;
                outPinned[v] = 1;
            }
        };
        touch(ia, ib);
        touch(ib, ic);
        touch(ic, ia);
    }
}

bool MassSpringSystem::build(const geometry::Mesh& mesh, const std::vector<analysis::TriangleWeakness>& triWeakness,
                             const MassSpringParams& params, bool fixBoundaryVertices) {
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

    std::vector<uint8_t> pin;
    if (fixBoundaryVertices) collectBoundaryVertices(mesh, pin);
    else
        pin.assign(V, 0);

    restPositions_.resize(V);
    vertices_.resize(V);
    for (size_t i = 0; i < V; ++i) {
        restPositions_[i] = mesh.positions[i];
        vertices_[i].position = mesh.positions[i];
        vertices_[i].velocity = glm::vec3(0.f);
        vertices_[i].force = glm::vec3(0.f);
        vertices_[i].mass = 1.f;
        vertices_[i].fixed = pin[i] != 0;
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

void MassSpringSystem::setExternalLoads01(float speed01, float accelLong01, float cornering01) {
    speed01 = std::clamp(speed01, 0.f, 1.f);
    accelLong01 = std::clamp(accelLong01, 0.f, 1.f);
    cornering01 = std::clamp(cornering01, 0.f, 1.f);
    float s = params_.externalForceScale;
    // Forward acceleration / cruise drag proxy → +Z; vertical bumps / gravity proxy → −Y; cornering → ±X
    externalAcceleration_ = glm::vec3(cornering01 * s, -accelLong01 * s * 1.1f, speed01 * s);
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
        if (!va.fixed) va.force += f;
        if (!vb.fixed) vb.force -= f;
    }

    for (size_t i = 0; i < vertices_.size(); ++i) {
        SimVertex& v = vertices_[i];
        if (v.fixed) continue;
        v.force += externalAcceleration_ * v.mass;
    }
}

void MassSpringSystem::integrateExplicitEuler(float dt) {
    const float damp = std::clamp(params_.damping, 0.f, 50.f);
    for (size_t i = 0; i < vertices_.size(); ++i) {
        SimVertex& v = vertices_[i];
        if (v.fixed) {
            v.velocity = glm::vec3(0.f);
            v.position = restPositions_[i];
            continue;
        }
        glm::vec3 a = v.force / std::max(v.mass, 1e-6f);
        v.velocity += a * dt;
        v.velocity *= std::exp(-damp * dt);
        v.position += v.velocity * dt;
    }
}

void MassSpringSystem::clampDisplacements() {
    float maxD = params_.maxDisplacement;
    if (maxD <= 0.f) return;
    for (size_t i = 0; i < vertices_.size(); ++i) {
        SimVertex& v = vertices_[i];
        if (v.fixed) continue;
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
    float ref = std::max(params_.strainReference, 1e-6f);

    auto edgeStrain = [&](uint32_t a, uint32_t b) -> float {
        if (a >= V || b >= V) return 0.f;
        float r = glm::distance(restPositions_[a], restPositions_[b]);
        if (r < 1e-20f) return 0.f;
        float L = glm::distance(vertices_[a].position, vertices_[b].position);
        return std::abs(L - r) / r;
    };

    for (size_t t = 0; t < triCount; ++t) {
        uint32_t ia = mesh.indices[t * 3], ib = mesh.indices[t * 3 + 1], ic = mesh.indices[t * 3 + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        float e0 = edgeStrain(ia, ib);
        float e1 = edgeStrain(ib, ic);
        float e2 = edgeStrain(ic, ia);
        float mx = std::max({e0, e1, e2});
        outPerTriangle[t] = std::clamp(mx / ref, 0.f, 1.f);
    }
}

} // namespace physisim::sim
