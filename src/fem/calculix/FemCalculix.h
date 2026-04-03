#pragma once

#include <string>

namespace physisim::fem {

struct FemInput;
struct FemResult;
struct TetrahedralMesh;

/// Writes `.inp`, runs `ccx <jobName>` in the job directory, parses `<jobName>.dat` into `FemResult`.
/// When `FemInput::workDirectory` is empty, creates a unique folder under the system temp directory
/// and removes it after a successful run unless `FemInput::keepWorkFiles` is true.
FemResult runCalculix(const TetrahedralMesh& mesh, const FemInput& input, std::string& errOut);

} // namespace physisim::fem
