#pragma once

#include <filesystem>
#include <string>

namespace physisim::fem {

struct FemResult;
struct TetrahedralMesh;

/// Parses CalculiX ASCII `.dat` produced with `*NODE PRINT, U` and `*EL PRINT, S` (see `CalculixInputWriter`).
bool parseCalculixDat(const std::filesystem::path& datPath, const TetrahedralMesh& mesh, FemResult& out,
                      std::string& errOut);

} // namespace physisim::fem
