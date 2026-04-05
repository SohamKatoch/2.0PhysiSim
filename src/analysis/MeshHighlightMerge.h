#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "analysis/TriangleWeakness.h"

namespace physisim::analysis {

struct GeometryAnalysisResult;

/// Combine deterministic triangle weakness with optional AI `design_actions` (interpretive overlay).
/// AI cannot reduce deterministic weakness; only increases or adds emphasis where hints apply.
/// If `outPerTriangleMerged` is non-null, receives full per-triangle [0,1] weakness (same order as mesh triangles).
void buildMergedViewportHighlights(const GeometryAnalysisResult& geo, const std::vector<float>& triangleStressProxy,
                                   const nlohmann::json& mergedReport, std::vector<uint32_t>& outTriangleIndices,
                                   std::vector<float>& outWeakness01, std::vector<float>* outPerTriangleMerged = nullptr,
                                   std::vector<TriangleWeakness>* outTriangleState = nullptr);

} // namespace physisim::analysis
