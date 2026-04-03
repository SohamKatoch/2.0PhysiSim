#include "fem/TetrahedralMesh.h"

namespace physisim::fem {

TetrahedralMesh TetrahedralMesh::singleCornerTetFromUnitCube() {
    TetrahedralMesh m;
    float h = 0.5f;
    m.nodes = {
        {-h, -h, -h},
        {h, -h, -h},
        {-h, h, -h},
        {-h, -h, h},
    };
    m.tets = {{{0, 1, 2, 3}}};
    return m;
}

} // namespace physisim::fem
