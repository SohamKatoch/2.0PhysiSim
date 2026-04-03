#pragma once

#include <filesystem>
#include <string>

namespace physisim::fem {

struct FemInput;
struct TetrahedralMesh;

/// Writes a minimal static *STATIC deck with C3D4 solids, *NODE PRINT / *EL PRINT for .dat parsing.
bool writeCalculixInp(const std::filesystem::path& inpPath, const TetrahedralMesh& mesh, const FemInput& input,
                      std::string& errOut);

} // namespace physisim::fem
