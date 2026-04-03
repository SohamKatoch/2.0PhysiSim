#pragma once

#include "fem/FemTypes.h"
#include "fem/TetrahedralMesh.h"

namespace physisim::fem {

/// Compares two `FemResult` values that correspond to the same `TetrahedralMesh` ordering.
/// `displacementRelativeL2Percent` is \(100 \cdot \|u_a-u_b\|_2 / \max(\|u_a\|_2,\|u_b\|_2,\epsilon)\).
/// `maxVonMisesDifference` is \(\max_i |\sigma_a(i)-\sigma_b(i)|\) over shared element indices.
/// `maxVonMisesRelativePercent` scales that delta by \(\max(|\sigma_a|,|\sigma_b|,\epsilon)\) at the argmax element.
ComparisonResult compareSolvers(const FemResult& a, const FemResult& b);

/// Same-sized zero fill (until an in-process elasticity backend exists).
FemResult makePlaceholderInternalResult(const TetrahedralMesh& mesh);

} // namespace physisim::fem
