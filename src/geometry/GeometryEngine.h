#pragma once

#include <memory>
#include <string>

#include "core/CommandSystem.h"
#include "core/Scene.h"

namespace physisim::geometry {

class GeometryEngine {
public:
    explicit GeometryEngine(core::Scene& scene);

    /// Applies validated commands only; AI never calls this directly except via orchestrated path.
    bool apply(const core::Command& cmd, std::string& errOut);

private:
    core::Scene& scene_;
};

} // namespace physisim::geometry
