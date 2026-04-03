#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

namespace physisim::analysis {

struct GeometryAnalysisResult;

/// Combine deterministic triangle weakness with optional AI `design_actions` (interpretive overlay).
/// AI cannot reduce deterministic weakness; only increases or adds emphasis where hints apply.
void buildMergedViewportHighlights(const GeometryAnalysisResult& geo, const std::vector<float>& triangleStressProxy,
                                   const nlohmann::json& mergedReport, std::vector<uint32_t>& outTriangleIndices,
                                   std::vector<float>& outWeakness01);

} // namespace physisim::analysis
