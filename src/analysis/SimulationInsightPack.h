#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::analysis {

/// Deterministic high-signal summary for Model 2 (optimizer). Engine = truth; this JSON is built only from CPU data.
nlohmann::json buildModel2SimulationPack(const geometry::Mesh& mesh, const std::vector<float>& triStrain01,
                                         const std::vector<float>& triGeo01, float meshUnitToMm,
                                         const std::string& materialLabel, const std::string& scenarioLabel,
                                         bool constraintsEnabled, float thicknessMmEstimate = 3.f);

/// Fingerprint slice for RAG retrieval (extends distance metric in AnalysisMemory).
nlohmann::json fingerprintFromSimulationPack(const nlohmann::json& pack);

} // namespace physisim::analysis
