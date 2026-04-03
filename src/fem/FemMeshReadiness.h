#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::fem {

enum class MeshFEMReadiness {
    READY,
    NEEDS_REPAIR,
    INVALID,
};

struct FemReadinessIssue {
    std::string code;
    std::string detail;
    /// "invalid" | "repair" — drives classification.
    std::string severityClass;
};

struct FemReadinessReport {
    MeshFEMReadiness readiness{MeshFEMReadiness::READY};
    std::vector<FemReadinessIssue> issues;
    std::vector<std::string> suggestions;

    /// `status`: "ok" | "warning" | "blocked" for command / log consumers.
    nlohmann::json toJson() const;
};

/// Full deterministic preflight (topology, triangle quality, self-intersection where feasible).
FemReadinessReport evaluateFEMReadiness(const geometry::Mesh& mesh);

/// Convenience: classification only.
MeshFEMReadiness checkFEMReadiness(const geometry::Mesh& mesh);

} // namespace physisim::fem
