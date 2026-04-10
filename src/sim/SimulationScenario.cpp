#include "sim/SimulationScenario.h"

#include <algorithm>
#include <cmath>

namespace physisim::sim {

namespace {

constexpr float kMphToMps = 0.44704f;

float clamp01(float x) { return std::clamp(x, 0.f, 1.f); }

/// Converts SI-ish accelerations into solver magnitude (positions in mesh units; dt ~ frame time).
float accelScale(float meshUnitToMm) {
    const float mu = std::max(meshUnitToMm, 1e-3f);
    const float metersPerMeshUnit = mu * 1e-3f;
    return std::max(0.08f / std::max(metersPerMeshUnit, 1e-6f), 8.f);
}

} // namespace

void scenarioToVisualizationSliders(const SimulationScenario& s, float& speed01, float& accel01, float& corner01) {
    const float inten = clamp01(s.intensity);
    const float spd = clamp01(s.speed_mph / 120.f);
    switch (s.type) {
    case ScenarioType::Highway:
        speed01 = spd * inten;
        accel01 = 0.12f * inten;
        corner01 = 0.f;
        break;
    case ScenarioType::Braking:
        speed01 = spd * 0.35f * inten;
        accel01 = inten;
        corner01 = 0.f;
        break;
    case ScenarioType::Cornering:
        speed01 = spd * 0.55f * inten;
        accel01 = 0.2f * inten;
        corner01 = inten;
        break;
    case ScenarioType::Bump:
        speed01 = spd * 0.25f * inten;
        accel01 = 0.85f * inten;
        corner01 = 0.15f * inten;
        break;
    }
}

glm::vec3 scenarioAccelerationSolver(const SimulationScenario& s, double timeSeconds, float meshUnitToMm) {
    const float scale = accelScale(meshUnitToMm) * clamp01(s.intensity);
    const float v = kMphToMps * std::max(s.speed_mph, 0.f);
    const float dur = std::max(s.duration_s, 0.05f);
    const double phase = 2.0 * 3.14159265358979323846 * timeSeconds / static_cast<double>(dur);

    glm::vec3 a{0.f};

    switch (s.type) {
    case ScenarioType::Highway: {
        const float drag = -0.018f * v * std::abs(v) * scale;
        a.z = drag;
        a.y = 0.35f * scale * static_cast<float>(std::sin(phase));
        break;
    }
    case ScenarioType::Braking:
        a.z = -9.0f * scale;
        break;
    case ScenarioType::Cornering:
        a.x = 6.5f * scale;
        break;
    case ScenarioType::Bump: {
        const double cycle = std::max(static_cast<double>(dur), 0.15);
        const double t = std::fmod(std::max(timeSeconds, 0.0), cycle);
        const double pulseW = 0.06 * std::max(1.0, cycle / dur);
        if (t < pulseW)
            a.y = 55.f * scale * static_cast<float>(std::sin((t / pulseW) * 3.14159265358979323846));
        break;
    }
    }

    return a;
}

} // namespace physisim::sim
