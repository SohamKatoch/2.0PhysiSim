#include "fem/FemCompare.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "fem/TetrahedralMesh.h"

namespace physisim::fem {

FemResult makePlaceholderInternalResult(const TetrahedralMesh& mesh) {
    FemResult r;
    r.displacement.assign(mesh.nodes.size(), glm::vec3(0.f));
    r.vonMises.assign(mesh.tets.size(), 0.f);
    r.ok = true;
    r.diagnosticLog = "placeholder internal FEM (zeros)";
    return r;
}

ComparisonResult compareSolvers(const FemResult& a, const FemResult& b) {
    ComparisonResult c{};
    constexpr double eps = 1e-30;

    if (a.displacement.size() != b.displacement.size()) return c;

    double diff2 = 0.0;
    double na2 = 0.0;
    double nb2 = 0.0;
    for (size_t i = 0; i < a.displacement.size(); ++i) {
        glm::vec3 d = a.displacement[i] - b.displacement[i];
        diff2 += static_cast<double>(glm::dot(d, d));
        na2 += static_cast<double>(glm::dot(a.displacement[i], a.displacement[i]));
        nb2 += static_cast<double>(glm::dot(b.displacement[i], b.displacement[i]));
    }
    double denom = std::max(std::sqrt(na2), std::sqrt(nb2));
    if (denom < eps) denom = eps;
    c.displacementRelativeL2Percent = 100.0 * std::sqrt(diff2) / denom;

    if (a.vonMises.size() != b.vonMises.size()) return c;

    double maxAbs = 0.0;
    double maxRel = 0.0;
    for (size_t i = 0; i < a.vonMises.size(); ++i) {
        double da = static_cast<double>(a.vonMises[i]);
        double db = static_cast<double>(b.vonMises[i]);
        double ad = std::abs(da - db);
        maxAbs = std::max(maxAbs, ad);
        double scale = std::max(std::max(std::abs(da), std::abs(db)), eps);
        maxRel = std::max(maxRel, ad / scale);
    }
    c.maxVonMisesDifference = maxAbs;
    c.maxVonMisesRelativePercent = 100.0 * maxRel;
    return c;
}

} // namespace physisim::fem
