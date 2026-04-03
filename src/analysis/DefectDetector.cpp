#include "analysis/DefectDetector.h"

#include "ai/AnalysisClient.h"
#include "analysis/AnalysisMemory.h"
#include "analysis/FeedbackBuilder.h"
#include "analysis/GeometryAnalyzer.h"
#include "analysis/GroundTruth.h"
#include "analysis/HeuristicAnalyzer.h"
#include "analysis/MeshMetrics.h"
#include "geometry/Mesh.h"

namespace physisim::analysis {

void DefectDetector::setAnalysisModelHost(const std::string& host, int port) {
    host_ = host;
    port_ = port;
}

nlohmann::json DefectDetector::parseJsonObject(const std::string& text) {
    auto start = text.find('{');
    auto end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start) return {};
    try {
        return nlohmann::json::parse(text.substr(start, end - start + 1));
    } catch (...) {
        return {};
    }
}

DefectReport DefectDetector::evaluate(const geometry::Mesh& mesh, const DefectDetectorOptions& opts) {
    DefectReport out;
    auto geo = GeometryAnalyzer::analyze(mesh);
    nlohmann::json heur = HeuristicAnalyzer::run(mesh);

    MeshMetricsOptions mopt;
    mopt.meshUnitMeters = opts.meshUnitToMm * 1e-3f;
    mopt.densityKgPerM3 = opts.materialDensityKgPerM3;
    MeshMetricsResult meshMetrics = computeMeshMetrics(mesh, mopt);
    out.triangleStressProxy = std::move(meshMetrics.triangleStressProxy);

    nlohmann::json groundTruth =
        buildGroundTruth(heur, geo.report, opts.meshUnitToMm, &meshMetrics.json);
    nlohmann::json fp = AnalysisMemory::fingerprint(heur, groundTruth);

    nlohmann::json summary;
    summary["heuristics"] = heur;
    summary["geometry"] = geo.report;
    summary["metrics"] = meshMetrics.json;
    summary["ground_truth"] = groundTruth;

    out.merged = nlohmann::json::object();
    out.merged["deterministic"] = geo.report;
    out.merged["heuristics"] = heur;
    out.merged["metrics"] = meshMetrics.json;
    out.merged["ground_truth"] = groundTruth;
    out.merged["fingerprint"] = fp;

    AnalysisMemory memory(opts.memoryDirectory);
    std::string ragBlock;
    if (opts.useRag) {
        ragBlock = memory.retrieveContextForPrompt(fp, 3);
        try {
            out.merged["rag_retrieved"] = nlohmann::json::parse(ragBlock.empty() ? "[]" : ragBlock);
        } catch (...) {
            out.merged["rag_retrieved"] = nlohmann::json::array();
        }
    }

    nlohmann::json aiIssues = nlohmann::json::array();

    if (!opts.useAi) {
        out.merged["ai_issues"] = aiIssues;
        out.merged["feedback"] =
            buildFeedbackPayload(aiIssues, groundTruth, geo.report);
        if (opts.persistCase) {
            std::string label = opts.caseLabel.empty() ? "model" : opts.caseLabel;
            memory.saveCase(label, fp, out.merged);
        }
        return out;
    }

    ai::AnalysisClientConfig cfg;
    cfg.host = host_;
    cfg.port = port_;
    ai::AnalysisClient client(cfg);

    std::string aiText;
    if (!client.analyzeFeatures(summary.dump(), aiText, out.lastError, ragBlock)) {
        out.merged["ai_issues"] = aiIssues;
        out.merged["ai_error"] = out.lastError;
        out.merged["feedback"] =
            buildFeedbackPayload(aiIssues, groundTruth, geo.report);
        if (opts.persistCase) {
            std::string label = opts.caseLabel.empty() ? "model" : opts.caseLabel;
            memory.saveCase(label, fp, out.merged);
        }
        return out;
    }
    out.aiRaw = aiText;
    nlohmann::json parsed = parseJsonObject(aiText);
    if (!parsed.empty() && parsed.is_object()) out.merged["ai_parsed"] = parsed;
    if (!parsed.empty() && parsed.contains("issues"))
        aiIssues = parsed["issues"];
    else if (parsed.is_array())
        aiIssues = parsed;
    out.merged["ai_issues"] = aiIssues;
    if (parsed.is_object() && parsed.contains("design_actions") && parsed["design_actions"].is_array())
        out.merged["design_actions"] = parsed["design_actions"];

    nlohmann::json feedback = buildFeedbackPayload(aiIssues, groundTruth, geo.report);
    out.merged["feedback"] = feedback;

    if (opts.feedbackLoop) {
        std::string refText;
        std::string refErr;
        if (client.refineWithFeedback(feedback.dump(), refText, refErr)) {
            out.refinementRaw = refText;
            nlohmann::json refParsed = parseJsonObject(refText);
            if (!refParsed.empty())
                out.merged["ai_refinement"] = refParsed;
            else
                out.merged["ai_refinement_raw"] = refText;
        } else {
            out.merged["ai_refinement_error"] = refErr;
        }
    }

    if (opts.persistCase) {
        std::string label = opts.caseLabel.empty() ? "model" : opts.caseLabel;
        memory.saveCase(label, fp, out.merged);
    }

    return out;
}

} // namespace physisim::analysis
