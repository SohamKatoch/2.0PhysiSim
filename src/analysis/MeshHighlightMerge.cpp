#include "analysis/MeshHighlightMerge.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <nlohmann/json.hpp>

#include "analysis/GeometryAnalyzer.h"

namespace physisim::analysis {

namespace {

float severityToWeakness(int sev) {
    return static_cast<float>(std::clamp(sev, 1, 5) - 1) / 4.f;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

void buildMergedViewportHighlights(const GeometryAnalysisResult& geo, const std::vector<float>& triangleStressProxy,
                                   const nlohmann::json& mergedReport, std::vector<uint32_t>& outTriangleIndices,
                                   std::vector<float>& outWeakness01) {
    outTriangleIndices.clear();
    outWeakness01.clear();

    const auto& base = geo.triWeaknessAll;
    if (base.empty()) return;

    std::vector<float> merged(base.begin(), base.end());

    const nlohmann::json* actions = nullptr;
    if (mergedReport.contains("design_actions") && mergedReport["design_actions"].is_array())
        actions = &mergedReport["design_actions"];
    else if (mergedReport.contains("ai_parsed") && mergedReport["ai_parsed"].is_object() &&
             mergedReport["ai_parsed"].contains("design_actions") &&
             mergedReport["ai_parsed"]["design_actions"].is_array())
        actions = &mergedReport["ai_parsed"]["design_actions"];

    std::vector<float> stress = triangleStressProxy;
    if (stress.size() != merged.size()) stress.clear();

    float stressThresh = 0.f;
    if (!stress.empty()) {
        std::vector<float> sorted = stress;
        auto mid = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() * 3 / 4);
        std::nth_element(sorted.begin(), mid, sorted.end());
        stressThresh = sorted.empty() ? 0.f : *mid;
    }

    auto thinPred = [&](size_t t) { return t < merged.size() && merged[t] > 0.02f && base[t] > 0.02f; };

    if (actions) {
        for (const auto& act : *actions) {
            if (!act.is_object()) continue;
            int sev = 3;
            if (act.contains("severity") && act["severity"].is_number_integer()) sev = act["severity"].get<int>();
            float w = severityToWeakness(sev);

            std::string hint = "general";
            if (act.contains("region_hint") && act["region_hint"].is_string())
                hint = lower(act["region_hint"].get<std::string>());

            std::string atype = "";
            if (act.contains("action_type") && act["action_type"].is_string())
                atype = lower(act["action_type"].get<std::string>());

            for (size_t t = 0; t < merged.size(); ++t) {
                float boost = 0.f;
                if (hint == "thin_wall" || hint == "thin_feature") {
                    if (thinPred(t)) boost = w * 0.85f + base[t] * 0.15f;
                } else if (hint == "stress_hotspot" || hint == "high_curvature") {
                    if (!stress.empty() && t < stress.size() && stress[t] >= stressThresh)
                        boost = std::max(w * 0.5f + stress[t] * 0.5f, stress[t]);
                } else if (hint == "boundary" || hint == "open_boundary") {
                    if (t < base.size() && base[t] >= severityToWeakness(2) * 0.9f) boost = std::max(boost, w);
                } else if (hint == "non_manifold") {
                    if (t < base.size() && base[t] >= severityToWeakness(5) * 0.95f) boost = std::max(boost, w);
                } else if (hint == "normals" || hint == "inverted_normals") {
                    if (t < base.size() && base[t] > 0.15f && base[t] < 0.95f && base[t] > 0.2f)
                        boost = std::max(boost, w * 0.6f + base[t] * 0.4f);
                } else if (hint == "global" || hint == "general") {
                    if (base[t] > 0.05f) boost = std::max(boost, w * 0.35f + base[t] * 0.65f);
                }

                if ((atype == "thickness" || atype == "thickness_reduction" || atype == "edge_smooth" ||
                     atype == "cutout" || atype == "weight_reduction") &&
                    hint == "general" && base[t] > 0.08f)
                    boost = std::max(boost, w * 0.4f + base[t] * 0.6f);

                if (boost > 0.f) merged[t] = std::clamp(std::max(merged[t], boost), 0.f, 1.f);
            }
        }
    }

    for (size_t t = 0; t < merged.size(); ++t) {
        if (merged[t] > 1e-5f) {
            outTriangleIndices.push_back(static_cast<uint32_t>(t));
            outWeakness01.push_back(std::clamp(merged[t], 0.f, 1.f));
        }
    }
}

} // namespace physisim::analysis
