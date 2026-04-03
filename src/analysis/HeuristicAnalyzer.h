#pragma once

#include <nlohmann/json.hpp>

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::analysis {

class HeuristicAnalyzer {
public:
    static nlohmann::json run(const geometry::Mesh& mesh);
};

} // namespace physisim::analysis
