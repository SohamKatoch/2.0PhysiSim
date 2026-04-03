#include "ai/AnalysisClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace physisim::ai {

AnalysisClient::AnalysisClient(AnalysisClientConfig cfg) : cfg_(std::move(cfg)) {}

bool AnalysisClient::analyzeFeatures(const std::string& meshSummaryJson, std::string& outText,
                                       std::string& err, const std::string& ragContext) {
    httplib::Client cli(cfg_.host.c_str(), cfg_.port);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(cfg_.timeoutSeconds, 0);

    std::string prompt =
        "You are a mechanical design assistant. Given the following mesh analysis summary (JSON), "
        "list predicted manufacturing/design risks AND propose optional design improvements. "
        "Treat `ground_truth`, `deterministic`, and `metrics` as authoritative physics/measurements from the "
        "engine — never contradict them. You only interpret and suggest; the engine overrides any conflict. "
        "Use `metrics` (volume, surface area, mass if present, center of mass, laplacian_stress_proxy) for "
        "quantitative reasoning (e.g. weight reduction vs stiffness tradeoffs) in qualitative terms only — "
        "do not invent new numbers. "
        "Respond with ONLY valid JSON: "
        "{\"issues\":[{\"id\":string,\"severity\":number,\"summary\":string,\"suggestion\":string,"
        "\"confidence\":number}],"
        "\"design_actions\":["
        "{\"id\":string,\"action_type\":string,\"severity\":number,\"rationale\":string,\"region_hint\":string}"
        "]}\n"
        "action_type must be one of: thickness_reduction, edge_smooth, cutout, weight_reduction, general. "
        "region_hint must be one of: thin_wall, stress_hotspot, boundary, non_manifold, normals, general. "
        "Map suggestions to region_hint so the viewport can emphasize the right triangles (deterministic "
        "geometry is merged with your hints). If no design change is appropriate, use an empty design_actions "
        "array.\n";
    if (!ragContext.empty()) {
        prompt += "SIMILAR_PAST_CASES_JSON (pattern memory only, not authoritative):\n" + ragContext + "\n";
    }
    prompt += "DATA:\n" + meshSummaryJson;

    nlohmann::json body;
    body["model"] = cfg_.model;
    body["prompt"] = prompt;
    body["stream"] = false;

    auto res = cli.Post("/api/generate", body.dump(), "application/json");
    if (!res) {
        err = "HTTP error (analysis)";
        return false;
    }
    if (res->status != 200) {
        err = "Analysis HTTP " + std::to_string(res->status);
        return false;
    }
    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("response") && j["response"].is_string()) {
            outText = j["response"].get<std::string>();
            return true;
        }
        err = "Unexpected analysis response";
        return false;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

bool AnalysisClient::refineWithFeedback(const std::string& feedbackPayloadJson, std::string& outText,
                                          std::string& err) {
    httplib::Client cli(cfg_.host.c_str(), cfg_.port);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(cfg_.timeoutSeconds, 0);

    std::string prompt =
        "You assist engineers with CAD mesh review. You receive JSON with: ai_prediction_summary, "
        "engine_results (AUTHORITATIVE ground truth — never contradict or replace these measurements), "
        "validation (agreement, missed_by_ai, possible_ai_false_positives).\n"
        "Respond with ONLY valid JSON:\n"
        "{\"refined_summary\":string,\"what_ai_got_right\":[],\"what_ai_missed\":[],"
        "\"calibrated_severity_notes\":string,\"suggested_next_checks\":[],"
        "\"design_actions_review\":string,"
        "\"disclaimer\":\"Engine measurements are ground truth; this is interpretive only.\"}\n"
        "Comment on whether proposed design_actions remain consistent with engine_results and physical "
        "constraints (still proposals only). Do not invent numeric measurements; reference engine_results when "
        "citing numbers.\n"
        "FEEDBACK:\n" +
        feedbackPayloadJson;

    nlohmann::json body;
    body["model"] = cfg_.model;
    body["prompt"] = prompt;
    body["stream"] = false;

    auto res = cli.Post("/api/generate", body.dump(), "application/json");
    if (!res) {
        err = "HTTP error (refinement)";
        return false;
    }
    if (res->status != 200) {
        err = "Refinement HTTP " + std::to_string(res->status);
        return false;
    }
    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("response") && j["response"].is_string()) {
            outText = j["response"].get<std::string>();
            return true;
        }
        err = "Unexpected refinement response";
        return false;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

} // namespace physisim::ai
