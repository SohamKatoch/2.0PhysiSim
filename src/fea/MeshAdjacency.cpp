#include "fea/MeshAdjacency.h"

#include <set>
#include <vector>

#include "geometry/Mesh.h"

namespace physisim::fea {

void buildUndirectedNeighborCsr(const geometry::Mesh& mesh, std::vector<uint32_t>& neighOff,
                                std::vector<uint32_t>& neighIdx) {
    neighOff.clear();
    neighIdx.clear();
    const size_t V = mesh.positions.size();
    if (V == 0) return;

    std::vector<std::set<uint32_t>> adj(V);
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        uint32_t ia = mesh.indices[t], ib = mesh.indices[t + 1], ic = mesh.indices[t + 2];
        if (ia >= V || ib >= V || ic >= V) continue;
        auto link = [&](uint32_t a, uint32_t b) {
            if (a != b) {
                adj[a].insert(b);
                adj[b].insert(a);
            }
        };
        link(ia, ib);
        link(ib, ic);
        link(ic, ia);
    }

    neighOff.resize(V + 1);
    uint32_t cur = 0;
    for (size_t v = 0; v < V; ++v) {
        neighOff[v] = cur;
        for (uint32_t n : adj[v]) neighIdx.push_back(n);
        cur += static_cast<uint32_t>(adj[v].size());
    }
    neighOff[V] = cur;
}

} // namespace physisim::fea
