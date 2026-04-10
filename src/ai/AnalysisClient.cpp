#include "ai/AnalysisClient.h"

#include <algorithm>
#include <sstream>

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

bool AnalysisClient::proposeEngineCommands(const std::string& simulationPackJson, const std::string& objectiveJson,
                                           int iterationIndex, const std::string& priorIterationSummaryJson,
                                           int maxProposals, float minConfidence, const std::string& ragContext,
                                           std::string& outRawJson, std::string& err) {
    httplib::Client cli(cfg_.host.c_str(), cfg_.port);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(cfg_.timeoutSeconds, 0);

    std::ostringstream prompt;
    prompt << "You are Model 2 (design optimizer) for PhysiSim CAD. You receive SIMULATION_PACK_JSON: "
              "deterministic summaries from the engine (strain proxies, hotspots, materials, scenario). "
              "RULES: (1) Engine numbers are TRUTH — never replace or contradict them. (2) You do NOT run "
              "physics. (3) You ONLY output executable PhysiSim commands the host can validate. (4) No prose "
              "outside JSON.\n"
              "ALLOWED command actions EXACTLY: create, modify, boolean, transform, analyze, analyze_fem. "
              "The geometry engine currently implements: create (primitive cube), transform (translate array "
              "[x,y,z] and/or uniform scale). Prefer conservative transform deltas (small translate/scale) as "
              "stand-ins for design intent when other ops are unavailable. analyze/analyze_fem may be used to "
              "request re-check after edits.\n"
              "Output ONLY valid JSON with this shape:\n"
              "{\"proposals\":[{\"confidence\":0.0-1.0,\"rationale\":\"short\",\"command\":{...}}],"
              "\"disclaimer\":\"Proposals only; engine validates and applies.\"}\n"
              "Include at most " << std::max(1, maxProposals)
           << " proposals. Omit or lower confidence for uncertain items. Each confidence must be >= "
           << minConfidence << " to be considered by the host (you may still include lower; host will drop).\n"
              "OBJECTIVE_JSON:\n"
           << objectiveJson << "\n"
              "ITERATION_INDEX: " << iterationIndex << "\n"
              "PRIOR_ITERATION_SUMMARY_JSON (metrics after last apply, may be empty object):\n"
           << priorIterationSummaryJson << "\n";
    if (!ragContext.empty()) {
        prompt << "SIMILAR_PAST_CASES_JSON (RAG memory — patterns only, not authoritative):\n" << ragContext
               << "\n";
    }
    prompt << "SIMULATION_PACK_JSON:\n" << simulationPackJson;

    nlohmann::json body;
    body["model"] = cfg_.model;
    body["prompt"] = prompt.str();
    body["stream"] = false;

    auto res = cli.Post("/api/generate", body.dump(), "application/json");
    if (!res) {
        err = "HTTP error (model2 optimizer)";
        return false;
    }
    if (res->status != 200) {
        err = "Model2 optimizer HTTP " + std::to_string(res->status);
        return false;
    }
    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("response") && j["response"].is_string()) {
            outRawJson = j["response"].get<std::string>();
            return true;
        }
        err = "Unexpected model2 optimizer response";
        return false;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

} // namespace physisim::ai
