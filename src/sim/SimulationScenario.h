#pragma once

#include <glm/vec3.hpp>

namespace physisim::sim {

enum class ScenarioType { Highway, Braking, Cornering, Bump };

struct SimulationScenario {
    ScenarioType type{ScenarioType::Highway};
    float speed_mph{55.f};
    float duration_s{5.f};
    float intensity{1.f};
};

/// Maps scenario + sliders to legacy 0–1 kinematic proxies for velocityWeight / loadWeight (deterministic).
void scenarioToVisualizationSliders(const SimulationScenario& s, float& speed01, float& accel01, float& corner01);

/// Model-space effective acceleration for the toy solver (+Z forward, +X lateral, +Y up). Scales with mesh units.
glm::vec3 scenarioAccelerationSolver(const SimulationScenario& s, double timeSeconds, float meshUnitToMm);

} // namespace physisim::sim
