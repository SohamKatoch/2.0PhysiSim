#pragma once

#include <cstdint>
#include <vector>

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::fea {

/// Undirected 1-ring CSR: for each vertex v, neighIdx[neighOff[v] .. neighOff[v+1]) are adjacent vertex indices.
void buildUndirectedNeighborCsr(const geometry::Mesh& mesh, std::vector<uint32_t>& neighOff,
                                std::vector<uint32_t>& neighIdx);

} // namespace physisim::fea
