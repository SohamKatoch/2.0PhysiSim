#include "fem/FemMeshReadiness.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "geometry/Mesh.h"

namespace physisim::fem {

namespace {

constexpr float kMaxTriangleAspect = 40.f;
constexpr float kThinGlobalEdgeRatio = 1e-4f;
constexpr uint64_t kMaxIntersectionPairTests = 5'000'000ull;

float bboxDiagonal(const geometry::Mesh& mesh) {
    if (mesh.positions.empty()) return 1.f;
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    for (const auto& p : mesh.positions) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    return std::max(glm::length(mx - mn), 1e-8f);
}

glm::vec3 triNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return glm::cross(b - a, c - a);
}

float triArea(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return 0.5f * glm::length(triNormal(a, b, c));
}

bool pointInTri3d(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float eps) {
    glm::vec3 v0 = b - a;
    glm::vec3 v1 = c - a;
    glm::vec3 v2 = p - a;
    float d00 = glm::dot(v0, v0);
    float d01 = glm::dot(v0, v1);
    float d11 = glm::dot(v1, v1);
    float d20 = glm::dot(v2, v0);
    float d21 = glm::dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < eps * eps) return false;
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.f - v - w;
    const float t = -1e-5f;
    return u >= t && v >= t && w >= t;
}

bool segTriIntersect(const glm::vec3& p, const glm::vec3& q, const glm::vec3& a, const glm::vec3& b,
                     const glm::vec3& c, float eps) {
    glm::vec3 n = triNormal(a, b, c);
    float nl = glm::length(n);
    if (nl < eps) return false;
    n /= nl;
    float d = glm::dot(n, a);
    float dp = glm::dot(n, p) - d;
    float dq = glm::dot(n, q) - d;
    if (std::abs(dp) < eps && std::abs(dq) < eps) {
        glm::vec3 u = glm::abs(n);
        int ax = 0, ay = 1;
        if (u.x >= u.y && u.x >= u.z) {
            ax = 1;
            ay = 2;
        } else if (u.y >= u.x && u.y >= u.z) {
            ax = 0;
            ay = 2;
        }
        auto proj = [&](const glm::vec3& v) {
            return glm::vec2(v[ax], v[ay]);
        };
        glm::vec2 P0 = proj(p), P1 = proj(q);
        glm::vec2 A = proj(a), B = proj(b), C = proj(c);
        auto segSeg2d = [&](glm::vec2 a0, glm::vec2 a1, glm::vec2 b0, glm::vec2 b1) {
            auto cross2 = [](glm::vec2 u, glm::vec2 v) { return u.x * v.y - u.y * v.x; };
            glm::vec2 r = a1 - a0, s = b1 - b0;
            float cr = cross2(r, s);
            glm::vec2 dlt = b0 - a0;
            if (std::abs(cr) < 1e-12f) return false;
            float t = cross2(dlt, s) / cr;
            float uu = cross2(dlt, r) / cr;
            return t >= -1e-6f && t <= 1.f + 1e-6f && uu >= -1e-6f && uu <= 1.f + 1e-6f;
        };
        if (segSeg2d(P0, P1, A, B) || segSeg2d(P0, P1, B, C) || segSeg2d(P0, P1, C, A)) return true;
        return pointInTri3d(p, a, b, c, eps) || pointInTri3d(q, a, b, c, eps);
    }
    if (dp * dq > 0.f && std::abs(dp) > eps && std::abs(dq) > eps) return false;
    float denom = dp - dq;
    if (std::abs(denom) < eps) return false;
    float t = dp / denom;
    if (t < -1e-5f || t > 1.f + 1e-5f) return false;
    glm::vec3 x = p + t * (q - p);
    return pointInTri3d(x, a, b, c, eps);
}

bool trianglesIntersectNonCoplanar(const glm::vec3& a0, const glm::vec3& a1, const glm::vec3& a2,
                                   const glm::vec3& b0, const glm::vec3& b1, const glm::vec3& b2, float eps) {
    if (segTriIntersect(a0, a1, b0, b1, b2, eps)) return true;
    if (segTriIntersect(a1, a2, b0, b1, b2, eps)) return true;
    if (segTriIntersect(a2, a0, b0, b1, b2, eps)) return true;
    if (segTriIntersect(b0, b1, a0, a1, a2, eps)) return true;
    if (segTriIntersect(b1, b2, a0, a1, a2, eps)) return true;
    if (segTriIntersect(b2, b0, a0, a1, a2, eps)) return true;
    if (pointInTri3d(a0, b0, b1, b2, eps) || pointInTri3d(a1, b0, b1, b2, eps) || pointInTri3d(a2, b0, b1, b2, eps))
        return true;
    if (pointInTri3d(b0, a0, a1, a2, eps) || pointInTri3d(b1, a0, a1, a2, eps) || pointInTri3d(b2, a0, a1, a2, eps))
        return true;
    return false;
}

bool trianglesIntersect(const glm::vec3& a0, const glm::vec3& a1, const glm::vec3& a2, const glm::vec3& b0,
                        const glm::vec3& b1, const glm::vec3& b2, float eps) {
    glm::vec3 n1 = triNormal(a0, a1, a2);
    glm::vec3 n2 = triNormal(b0, b1, b2);
    if (glm::length(n1) < eps || glm::length(n2) < eps) return false;
    n1 = glm::normalize(n1);
    n2 = glm::normalize(n2);
    if (std::abs(glm::dot(n1, n2)) > 1.f - 1e-4f &&
        std::abs(glm::dot(n1, b0 - a0)) < eps * 10.f) {
        glm::vec3 u = glm::abs(n1);
        int ax = 0, ay = 1;
        if (u.x >= u.y && u.x >= u.z) {
            ax = 1;
            ay = 2;
        } else if (u.y >= u.x && u.y >= u.z) {
            ax = 0;
            ay = 2;
        }
        auto proj = [&](const glm::vec3& v) { return glm::vec2(v[ax], v[ay]); };
        glm::vec2 A0 = proj(a0), A1 = proj(a1), A2 = proj(a2);
        glm::vec2 B0 = proj(b0), B1 = proj(b1), B2 = proj(b2);
        auto segSeg2d = [](glm::vec2 p, glm::vec2 q, glm::vec2 r, glm::vec2 s) {
            auto cross2 = [](glm::vec2 u, glm::vec2 v) { return u.x * v.y - u.y * v.x; };
            glm::vec2 rd = q - p, sd = s - r;
            float cr = cross2(rd, sd);
            glm::vec2 dlt = r - p;
            if (std::abs(cr) < 1e-14f) return false;
            float t = cross2(dlt, sd) / cr;
            float uu = cross2(dlt, rd) / cr;
            return t >= -1e-6f && t <= 1.f + 1e-6f && uu >= -1e-6f && uu <= 1.f + 1e-6f;
        };
        if (segSeg2d(A0, A1, B0, B1) || segSeg2d(A0, A1, B1, B2) || segSeg2d(A0, A1, B2, B0)) return true;
        if (segSeg2d(A1, A2, B0, B1) || segSeg2d(A1, A2, B1, B2) || segSeg2d(A1, A2, B2, B0)) return true;
        if (segSeg2d(A2, A0, B0, B1) || segSeg2d(A2, A0, B1, B2) || segSeg2d(A2, A0, B2, B0)) return true;
        return pointInTri3d(a0, b0, b1, b2, eps) || pointInTri3d(b0, a0, a1, a2, eps);
    }
    return trianglesIntersectNonCoplanar(a0, a1, a2, b0, b1, b2, eps);
}

bool shareVertex(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t b0, uint32_t b1, uint32_t b2) {
    return a0 == b0 || a0 == b1 || a0 == b2 || a1 == b0 || a1 == b1 || a1 == b2 || a2 == b0 || a2 == b1 ||
           a2 == b2;
}

struct TriBounds {
    glm::vec3 mn;
    glm::vec3 mx;
};

TriBounds triBounds(const geometry::Mesh& mesh, uint32_t ti) {
    size_t b = static_cast<size_t>(ti) * 3;
    uint32_t ia = mesh.indices[b], ib = mesh.indices[b + 1], ic = mesh.indices[b + 2];
    glm::vec3 a = mesh.positions[ia], c1 = mesh.positions[ib], c2 = mesh.positions[ic];
    glm::vec3 mn = glm::min(a, glm::min(c1, c2));
    glm::vec3 mx = glm::max(a, glm::max(c1, c2));
    return {mn, mx};
}

using EdgeKey = std::tuple<uint32_t, uint32_t>;

} // namespace

nlohmann::json FemReadinessReport::toJson() const {
    nlohmann::json j;
    std::string status;
    if (readiness == MeshFEMReadiness::INVALID)
        status = "blocked";
    else if (readiness == MeshFEMReadiness::NEEDS_REPAIR)
        status = "warning";
    else
        status = "ok";
    j["status"] = status;
    j["readiness"] = readiness == MeshFEMReadiness::READY         ? "READY"
                     : readiness == MeshFEMReadiness::NEEDS_REPAIR ? "NEEDS_REPAIR"
                                                                   : "INVALID";
    j["issues"] = nlohmann::json::array();
    for (const auto& is : issues) {
        j["issues"].push_back({{"code", is.code}, {"detail", is.detail}, {"severity_class", is.severityClass}});
    }
    j["suggestions"] = suggestions;
    return j;
}

FemReadinessReport evaluateFEMReadiness(const geometry::Mesh& mesh) {
    FemReadinessReport r;
    const float eps = bboxDiagonal(mesh) * 1e-7f;

    if (mesh.indices.size() < 3 || mesh.indices.size() % 3 != 0) {
        r.issues.push_back({"invalid_index_buffer", "Triangle index count is not a multiple of 3.", "invalid"});
        r.readiness = MeshFEMReadiness::INVALID;
        r.suggestions.push_back("Reload a valid STL or regenerate the surface mesh.");
        return r;
    }

    std::map<EdgeKey, int> edgeCount;
    auto addEdge = [&](uint32_t u, uint32_t v) {
        if (u > v) std::swap(u, v);
        edgeCount[{u, v}]++;
    };

    uint32_t nTri = static_cast<uint32_t>(mesh.indices.size() / 3);
    float minEdgeGlobal = std::numeric_limits<float>::max();
    float maxEdgeGlobal = 0.f;
    float maxAspectGlobal = 0.f;
    uint32_t degenerateCount = 0;

    for (uint32_t ti = 0; ti < nTri; ++ti) {
        size_t b = static_cast<size_t>(ti) * 3;
        uint32_t ia = mesh.indices[b], ib = mesh.indices[b + 1], ic = mesh.indices[b + 2];
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size()) {
            r.issues.push_back({"out_of_range_vertex", "A triangle references a missing vertex index.", "invalid"});
            r.readiness = MeshFEMReadiness::INVALID;
            r.suggestions.push_back("Repair index buffer or reload mesh.");
            return r;
        }
        addEdge(ia, ib);
        addEdge(ib, ic);
        addEdge(ic, ia);

        glm::vec3 a = mesh.positions[ia], c1 = mesh.positions[ib], c2 = mesh.positions[ic];
        float e0 = glm::distance(a, c1);
        float e1 = glm::distance(c1, c2);
        float e2 = glm::distance(c2, a);
        float me = std::min({e0, e1, e2});
        float Ma = std::max({e0, e1, e2});
        minEdgeGlobal = std::min(minEdgeGlobal, me);
        maxEdgeGlobal = std::max(maxEdgeGlobal, Ma);
        if (me > eps) maxAspectGlobal = std::max(maxAspectGlobal, Ma / me);

        float ar = triArea(a, c1, c2);
        if (ar < eps * eps) degenerateCount++;
    }

    int nonManifold = 0;
    int boundary = 0;
    for (const auto& [_, c] : edgeCount) {
        if (c > 2) nonManifold++;
        if (c == 1) boundary++;
    }

    if (nonManifold > 0) {
        r.issues.push_back({"non_manifold_edges",
                            std::to_string(nonManifold) + " undirected edges are shared by more than two triangles.",
                            "invalid"});
        r.readiness = MeshFEMReadiness::INVALID;
        r.suggestions.push_back("Split non-manifold edges or separate shells before tetrahedralization.");
    }

    if (degenerateCount > 0) {
        r.issues.push_back(
            {"degenerate_triangles", std::to_string(degenerateCount) + " triangles have near-zero area.", "invalid"});
        r.readiness = MeshFEMReadiness::INVALID;
        r.suggestions.push_back("Remove or fix degenerate triangles (remesh / cleanup).");
    }

    bool selfIntersect = false;
    if (r.readiness != MeshFEMReadiness::INVALID && nTri > 0) {
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(-std::numeric_limits<float>::max());
        for (uint32_t ti = 0; ti < nTri; ++ti) {
            TriBounds tb = triBounds(mesh, ti);
            mn = glm::min(mn, tb.mn);
            mx = glm::max(mx, tb.mx);
        }
        glm::vec3 ext = mx - mn;
        float cell = std::max(std::max({ext.x, ext.y, ext.z}) / 48.f, eps * 100.f);

        struct CellKey {
            int x, y, z;
            bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
        };
        struct CellHash {
            size_t operator()(const CellKey& k) const {
                return (static_cast<size_t>(static_cast<uint32_t>(k.x)) << 42) ^
                       (static_cast<size_t>(static_cast<uint32_t>(k.y)) << 21) ^
                       static_cast<size_t>(static_cast<uint32_t>(k.z));
            }
        };
        std::unordered_map<CellKey, std::vector<uint32_t>, CellHash> grid;

        auto cellOf = [&](const glm::vec3& p) {
            return CellKey{static_cast<int>(std::floor((p.x - mn.x) / cell)),
                           static_cast<int>(std::floor((p.y - mn.y) / cell)),
                           static_cast<int>(std::floor((p.z - mn.z) / cell))};
        };

        for (uint32_t ti = 0; ti < nTri; ++ti) {
            TriBounds tb = triBounds(mesh, ti);
            CellKey c0 = cellOf(tb.mn);
            CellKey c1 = cellOf(tb.mx);
            for (int cx = c0.x; cx <= c1.x; ++cx)
                for (int cy = c0.y; cy <= c1.y; ++cy)
                    for (int cz = c0.z; cz <= c1.z; ++cz) grid[{cx, cy, cz}].push_back(ti);
        }

        uint64_t tests = 0;
        for (const auto& [_, vec] : grid) {
            for (size_t i = 0; i < vec.size(); ++i) {
                for (size_t j = i + 1; j < vec.size(); ++j) {
                    if (++tests > kMaxIntersectionPairTests) {
                        r.issues.push_back({"self_intersection_scan_incomplete",
                                            "Intersection scan stopped after " +
                                                std::to_string(kMaxIntersectionPairTests) + " pair tests.",
                                            "repair"});
                        if (r.readiness == MeshFEMReadiness::READY) r.readiness = MeshFEMReadiness::NEEDS_REPAIR;
                        r.suggestions.push_back("Simplify or partition the mesh, then re-run FEM preflight.");
                        goto intersection_done;
                    }
                    uint32_t ti = vec[i], tj = vec[j];
                    size_t bi = static_cast<size_t>(ti) * 3, bj = static_cast<size_t>(tj) * 3;
                    uint32_t ia = mesh.indices[bi], ib = mesh.indices[bi + 1], ic = mesh.indices[bi + 2];
                    uint32_t ja = mesh.indices[bj], jb = mesh.indices[bj + 1], jc = mesh.indices[bj + 2];
                    if (shareVertex(ia, ib, ic, ja, jb, jc)) continue;
                    glm::vec3 a0 = mesh.positions[ia], a1 = mesh.positions[ib], a2 = mesh.positions[ic];
                    glm::vec3 b0 = mesh.positions[ja], b1 = mesh.positions[jb], b2 = mesh.positions[jc];
                    if (trianglesIntersect(a0, a1, a2, b0, b1, b2, eps)) {
                        selfIntersect = true;
                        goto intersection_done;
                    }
                }
            }
        }
    intersection_done:;
    }

    if (selfIntersect) {
        r.issues.push_back(
            {"self_intersection", "At least one pair of non-adjacent triangles intersects in 3D.", "invalid"});
        r.readiness = MeshFEMReadiness::INVALID;
        r.suggestions.push_back("Fix overlapping geometry or run a self-intersection repair in your CAD mesher.");
    }

    if (boundary > 0) {
        r.issues.push_back({"open_boundary",
                            std::to_string(boundary) + " boundary edges (mesh is not watertight).", "repair"});
        if (r.readiness == MeshFEMReadiness::READY) r.readiness = MeshFEMReadiness::NEEDS_REPAIR;
        r.suggestions.push_back("Close holes or export a closed solid before volume meshing.");
    }

    if (maxEdgeGlobal > eps && minEdgeGlobal / maxEdgeGlobal < kThinGlobalEdgeRatio) {
        r.issues.push_back({"thin_features_global",
                            "Global min/max edge ratio is very small (sliver / thin feature risk).", "repair"});
        if (r.readiness == MeshFEMReadiness::READY) r.readiness = MeshFEMReadiness::NEEDS_REPAIR;
        r.suggestions.push_back("Remesh with minimum edge length control or merge sliver triangles.");
    }

    if (maxAspectGlobal > kMaxTriangleAspect) {
        r.issues.push_back({"poor_triangle_aspect",
                            "Maximum triangle edge-length ratio is " + std::to_string(maxAspectGlobal) +
                                " (threshold " + std::to_string(kMaxTriangleAspect) + ").",
                            "repair"});
        if (r.readiness == MeshFEMReadiness::READY) r.readiness = MeshFEMReadiness::NEEDS_REPAIR;
        r.suggestions.push_back("Improve triangle quality (isotropic remesh / refine).");
    }

    return r;
}

MeshFEMReadiness checkFEMReadiness(const geometry::Mesh& mesh) { return evaluateFEMReadiness(mesh).readiness; }

} // namespace physisim::fem
