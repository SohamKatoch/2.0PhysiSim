#include "analysis/FeedbackBuilder.h"

#include <cctype>
#include <string>

namespace physisim::analysis {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool containsAny(const std::string& hay, const std::vector<const char*>& needles) {
    std::string h = lower(hay);
    for (const char* n : needles) {
        std::string nn = lower(std::string(n));
        if (h.find(nn) != std::string::npos) return true;
    }
    return false;
}

std::string issueText(const nlohmann::json& issue) {
    std::string parts;
    if (issue.contains("id") && issue["id"].is_string()) parts += issue["id"].get<std::string>() + " ";
    if (issue.contains("type") && issue["type"].is_string()) parts += issue["type"].get<std::string>() + " ";
    if (issue.contains("summary") && issue["summary"].is_string()) parts += issue["summary"].get<std::string>();
    return parts;
}

bool aiMentionsThin(const nlohmann::json& issues) {
    if (!issues.is_array()) return false;
    for (const auto& i : issues) {
        if (containsAny(issueText(i), {"thin", "sliver", "wall", "width"})) return true;
    }
    return false;
}

bool aiMentionsNonManifold(const nlohmann::json& issues) {
    if (!issues.is_array()) return false;
    for (const auto& i : issues) {
        if (containsAny(issueText(i), {"non-manifold", "nonmanifold"})) return true;
    }
    return false;
}

bool aiMentionsNormals(const nlohmann::json& issues) {
    if (!issues.is_array()) return false;
    for (const auto& i : issues) {
        if (containsAny(issueText(i), {"normal", "inverted", "flip"})) return true;
    }
    return false;
}

bool aiMentionsBoundary(const nlohmann::json& issues) {
    if (!issues.is_array()) return false;
    for (const auto& i : issues) {
        if (containsAny(issueText(i), {"boundary", "open", "watertight", "hole", "gap"})) return true;
    }
    return false;
}

} // namespace

nlohmann::json buildFeedbackPayload(const nlohmann::json& aiIssues, const nlohmann::json& groundTruth,
                                    const nlohmann::json& geometryDeterministicReport) {
    nlohmann::json fb;
    fb["schema"] = "physisim_analysis_feedback_v1";

    bool engThin = groundTruth.contains("flags") && groundTruth["flags"].value("thin_feature_or_sliver", false);
    bool engNm = groundTruth.contains("flags") && groundTruth["flags"].value("non_manifold", false);
    bool engBd = groundTruth.contains("flags") && groundTruth["flags"].value("open_boundary", false);
    bool engNorm = groundTruth.contains("flags") && groundTruth["flags"].value("inconsistent_normals", false);

    nlohmann::json pred;
    pred["thin_feature"] = aiMentionsThin(aiIssues);
    pred["non_manifold"] = aiMentionsNonManifold(aiIssues);
    pred["inverted_or_normals"] = aiMentionsNormals(aiIssues);
    pred["open_boundary"] = aiMentionsBoundary(aiIssues);
    fb["ai_prediction_summary"] = pred;
    fb["raw_ai_issues"] = aiIssues.is_array() ? aiIssues : nlohmann::json::array();

    fb["engine_results"] = groundTruth;

    nlohmann::json val;
    val["thin_feature_agreement"] = (pred["thin_feature"].get<bool>() == engThin);
    val["non_manifold_agreement"] = (pred["non_manifold"].get<bool>() == engNm);
    val["boundary_agreement"] = (pred["open_boundary"].get<bool>() == engBd);
    val["normals_agreement"] = (pred["inverted_or_normals"].get<bool>() == engNorm);

    val["prediction_correct"] =
        val["thin_feature_agreement"].get<bool>() && val["non_manifold_agreement"].get<bool>() &&
        val["boundary_agreement"].get<bool>() && val["normals_agreement"].get<bool>();

    nlohmann::json missed = nlohmann::json::array();
    if (engThin && !pred["thin_feature"].get<bool>()) missed.push_back("engine_detected_thin_feature_not_in_ai_summary");
    if (engNm && !pred["non_manifold"].get<bool>()) missed.push_back("engine_non_manifold_not_in_ai_summary");
    if (engBd && !pred["open_boundary"].get<bool>()) missed.push_back("engine_open_boundary_not_in_ai_summary");
    if (engNorm && !pred["inverted_or_normals"].get<bool>()) missed.push_back("engine_normal_issues_not_in_ai_summary");
    val["missed_by_ai"] = missed;

    nlohmann::json falsePos = nlohmann::json::array();
    if (!engThin && pred["thin_feature"].get<bool>()) falsePos.push_back("ai_mentioned_thin_feature_not_flagged_by_engine_heuristic");
    if (!engNm && pred["non_manifold"].get<bool>()) falsePos.push_back("ai_mentioned_non_manifold_engine_count_zero");
    if (!engBd && pred["open_boundary"].get<bool>()) falsePos.push_back("ai_mentioned_boundary_engine_count_zero");
    if (!engNorm && pred["inverted_or_normals"].get<bool>())
        falsePos.push_back("ai_mentioned_normals_engine_check_clean");
    val["possible_ai_false_positives"] = falsePos;

    bool underSeverity = false;
    if (aiIssues.is_array()) {
        for (const auto& i : aiIssues) {
            if (!i.contains("severity") || !i["severity"].is_number_integer()) continue;
            int sev = i["severity"].get<int>();
            if (engThin && containsAny(issueText(i), {"thin"}) && sev < 3) underSeverity = true;
            if (engNm && sev < 4) underSeverity = true;
        }
    }
    val["severity_possibly_underestimated"] = underSeverity;

    val["rule"] = "Ground truth from engine_results is authoritative; AI interprets only.";
    fb["validation"] = val;
    fb["geometry_engine_checks_ref"] = geometryDeterministicReport.contains("deterministic_checks")
                                           ? geometryDeterministicReport["deterministic_checks"]
                                           : nlohmann::json::array();

    return fb;
}

} // namespace physisim::analysis
