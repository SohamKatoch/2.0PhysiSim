#include "analysis/GroundTruth.h"

#include <algorithm>
#include <cmath>

namespace physisim::analysis {

static float getF(const nlohmann::json& j, const char* key, float def = 0.f) {
    if (!j.contains(key) || !j[key].is_number()) return def;
    return j[key].get<float>();
}

static int getI(const nlohmann::json& j, const char* key, int def = 0) {
    if (!j.contains(key) || !j[key].is_number_integer()) return def;
    return j[key].get<int>();
}

nlohmann::json buildGroundTruth(const nlohmann::json& heuristics, const nlohmann::json& geometryReport,
                                float meshUnitToMm, const nlohmann::json* meshMetrics) {
    nlohmann::json gt;
    gt["source"] = "geometry_engine";
    gt["authority"] = "ground_truth";
    gt["units"]["mesh"] = "model_units";
    gt["units"]["mm_scale"] = meshUnitToMm;
    gt["note"] = "All lengths below are in mesh units unless suffixed with _mm.";

    const float s = meshUnitToMm > 0.f ? meshUnitToMm : 1.f;

    float minEdge = getF(heuristics, "min_edge_length");
    float maxEdge = getF(heuristics, "max_edge_length");
    gt["measurements"]["min_edge_length_mesh"] = minEdge;
    gt["measurements"]["max_edge_length_mesh"] = maxEdge;
    gt["measurements"]["min_edge_length_mm"] = minEdge * s;
    gt["measurements"]["max_edge_length_mm"] = maxEdge * s;
    gt["measurements"]["edge_length_ratio"] = getF(heuristics, "edge_length_ratio", 1.f);

    if (heuristics.contains("extent") && heuristics["extent"].is_array() && heuristics["extent"].size() >= 3) {
        auto& e = heuristics["extent"];
        gt["measurements"]["extent_mesh"] = {e[0].get<float>(), e[1].get<float>(), e[2].get<float>()};
        gt["measurements"]["extent_mm"] = {e[0].get<float>() * s, e[1].get<float>() * s, e[2].get<float>() * s};
    }

    gt["topology"]["triangle_count"] = getI(heuristics, "triangle_count");
    gt["topology"]["vertex_count"] = getI(heuristics, "vertex_count");
    gt["topology"]["boundary_edge_count"] = getI(heuristics, "boundary_edge_count");
    gt["topology"]["non_manifold_edge_count"] = getI(heuristics, "non_manifold_edge_count");

    bool thinEngine = false;
    if (geometryReport.contains("deterministic_checks") && geometryReport["deterministic_checks"].is_array()) {
        for (const auto& c : geometryReport["deterministic_checks"]) {
            if (c.contains("type") && c["type"].is_string() && c["type"].get<std::string>() == "thin_feature")
                thinEngine = true;
        }
    }
    int hl = 0;
    if (geometryReport.contains("highlight_triangle_count") && geometryReport["highlight_triangle_count"].is_number_integer())
        hl = geometryReport["highlight_triangle_count"].get<int>();
    thinEngine = thinEngine || hl > 0;

    gt["flags"]["thin_feature_or_sliver"] = thinEngine;
    gt["flags"]["highlighted_suspect_triangles"] = hl;
    gt["flags"]["non_manifold"] = getI(heuristics, "non_manifold_edge_count") > 0;
    gt["flags"]["open_boundary"] = getI(heuristics, "boundary_edge_count") > 0;

    bool badNorm = false;
    if (geometryReport.contains("deterministic_checks") && geometryReport["deterministic_checks"].is_array()) {
        for (const auto& c : geometryReport["deterministic_checks"]) {
            if (!c.contains("type") || !c["type"].is_string()) continue;
            std::string t = c["type"].get<std::string>();
            if (t == "inverted_or_inconsistent_normals") badNorm = true;
        }
    }
    gt["flags"]["inconsistent_normals"] = badNorm;

    gt["thresholds"]["thin_wall_proxy_ratio"] = 0.08f;
    gt["thresholds"]["note"] = "edge_length_ratio below this suggests thin features vs longest edge (heuristic).";

    if (meshMetrics && meshMetrics->is_object()) {
        auto& m = gt["measurements"];
        if (meshMetrics->contains("surface_area_mesh_units") && (*meshMetrics)["surface_area_mesh_units"].is_number())
            m["surface_area_mesh_units"] = (*meshMetrics)["surface_area_mesh_units"].get<float>();
        if (meshMetrics->contains("signed_volume_mesh_units") && (*meshMetrics)["signed_volume_mesh_units"].is_number())
            m["signed_volume_mesh_units"] = (*meshMetrics)["signed_volume_mesh_units"].get<float>();
        if (meshMetrics->contains("volume_abs_mesh_units") && (*meshMetrics)["volume_abs_mesh_units"].is_number())
            m["volume_abs_mesh_units"] = (*meshMetrics)["volume_abs_mesh_units"].get<float>();
        if (meshMetrics->contains("volume_reliable_for_mass") && (*meshMetrics)["volume_reliable_for_mass"].is_boolean())
            m["volume_reliable_for_mass"] = (*meshMetrics)["volume_reliable_for_mass"].get<bool>();
        if (meshMetrics->contains("center_of_mass_mesh") && (*meshMetrics)["center_of_mass_mesh"].is_array() &&
            (*meshMetrics)["center_of_mass_mesh"].size() >= 3) {
            auto& c = (*meshMetrics)["center_of_mass_mesh"];
            m["center_of_mass_mesh"] = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
            float mm = meshUnitToMm > 0.f ? meshUnitToMm : 1.f;
            m["center_of_mass_mm"] = {c[0].get<float>() * mm, c[1].get<float>() * mm, c[2].get<float>() * mm};
        }
        if (meshMetrics->contains("mass_kg") && (*meshMetrics)["mass_kg"].is_number())
            m["mass_kg"] = (*meshMetrics)["mass_kg"].get<float>();
        if (meshMetrics->contains("laplacian_stress_proxy"))
            m["laplacian_stress_proxy"] = (*meshMetrics)["laplacian_stress_proxy"];
    }

    return gt;
}

} // namespace physisim::analysis
