#pragma once

#include <glm/vec3.hpp>

#include <optional>
#include <string>
#include <vector>

namespace physisim::fem {

/// Boundary conditions, material, loads, and process paths for an external FEM run.
struct FemInput {
    /// CalculiX driver (`ccx`); may be absolute. On Windows often `ccx.exe` or full path.
    std::string ccxExecutable = "ccx";
    /// Job stem: reads/writes `<workDirectory>/<jobName>.inp`, `.dat`, `.frd`, etc.
    std::string jobName = "physisim_fem";
    /// If empty, `runCalculix` uses a unique subdirectory under the system temp directory.
    std::string workDirectory;

    /// Young's modulus in MPa (consistent with mm geometry and forces in N).
    double youngModulusMpa = 210000.0;
    double poissonRatio = 0.3;
    /// Mass density (kg/m³); converted in .inp to tonne/mm³ when using mm geometry.
    double densityKgM3 = 7850.0;

    /// Nodes with all translational DOFs fixed (0-based indices).
    std::vector<uint32_t> fixedNodes;

    std::optional<uint32_t> loadNode;
    /// Point load components in N (global X,Y,Z).
    glm::dvec3 loadForceN{0.0, 0.0, 0.0};

    bool enableGravity = false;
    /// Downward acceleration magnitude (mm/s²) when `enableGravity` is true (mm-based model).
    double gravityMmPerS2 = 9810.0;

    /// If true, leaves the job directory on disk (including when a temp dir was created).
    bool keepWorkFiles = false;
};

struct FemResult {
    /// One von Mises–type scalar per tet (first integration point / element center proxy).
    std::vector<float> vonMises;
    /// Displacement per node, same ordering as source `TetrahedralMesh::nodes`.
    std::vector<glm::vec3> displacement;
    std::string diagnosticLog;
    bool ok{false};
};

struct ComparisonResult {
    /// L2 norm of displacement difference divided by max(||u_a||, ||u_b||, eps) as percent.
    double displacementRelativeL2Percent{0.0};
    double maxVonMisesDifference{0.0};
    /// |Δσ| / max(|σ_a|,|σ_b|, eps) as percent at the argmax element.
    double maxVonMisesRelativePercent{0.0};
};

} // namespace physisim::fem
