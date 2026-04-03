#pragma once

#include <nlohmann/json.hpp>

namespace physisim::analysis {

/// Authoritative measurements from the geometry engine (never overwritten by AI).
nlohmann::json buildGroundTruth(const nlohmann::json& heuristics, const nlohmann::json& geometryReport,
                                 float meshUnitToMm, const nlohmann::json* meshMetrics = nullptr);

} // namespace physisim::analysis
