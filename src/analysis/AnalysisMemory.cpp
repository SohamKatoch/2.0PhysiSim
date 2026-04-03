#include "analysis/AnalysisMemory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace physisim::analysis {

namespace fs = std::filesystem;

AnalysisMemory::AnalysisMemory(std::string storeDirectory) : dir_(std::move(storeDirectory)) {}

nlohmann::json AnalysisMemory::fingerprint(const nlohmann::json& heuristics, const nlohmann::json& groundTruth) {
    nlohmann::json f;
    int tri = 0;
    if (heuristics.contains("triangle_count") && heuristics["triangle_count"].is_number_integer())
        tri = heuristics["triangle_count"].get<int>();
    f["log1p_triangles"] = std::log1p(static_cast<float>(std::max(0, tri)));
    f["non_manifold"] = groundTruth.contains("flags") && groundTruth["flags"].value("non_manifold", false) ? 1.0f : 0.f;
    f["open_boundary"] = groundTruth.contains("flags") && groundTruth["flags"].value("open_boundary", false) ? 1.0f : 0.f;
    f["thin"] = groundTruth.contains("flags") && groundTruth["flags"].value("thin_feature_or_sliver", false) ? 1.0f : 0.f;
    float ratio = 1.f;
    if (heuristics.contains("edge_length_ratio") && heuristics["edge_length_ratio"].is_number())
        ratio = heuristics["edge_length_ratio"].get<float>();
    f["edge_length_ratio"] = ratio;
    return f;
}

float AnalysisMemory::l1Distance(const nlohmann::json& a, const nlohmann::json& b) {
    float d = 0.f;
    for (const char* k : {"log1p_triangles", "non_manifold", "open_boundary", "thin", "edge_length_ratio"}) {
        float x = a.contains(k) && a[k].is_number() ? a[k].get<float>() : 0.f;
        float y = b.contains(k) && b[k].is_number() ? b[k].get<float>() : 0.f;
        d += std::fabs(x - y);
    }
    return d;
}

std::string AnalysisMemory::retrieveContextForPrompt(const nlohmann::json& fingerprint, size_t topK) const {
    if (!fs::exists(dir_)) return "[]";

    struct Scored {
        float dist;
        nlohmann::json rec;
    };
    std::vector<Scored> all;

    for (const auto& entry : fs::directory_iterator(dir_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path());
        if (!in) continue;
        try {
            nlohmann::json rec;
            in >> rec;
            if (!rec.contains("fingerprint")) continue;
            float d = l1Distance(fingerprint, rec["fingerprint"]);
            all.push_back({d, std::move(rec)});
        } catch (...) {
        }
    }

    std::sort(all.begin(), all.end(), [](const Scored& a, const Scored& b) { return a.dist < b.dist; });

    nlohmann::json ctx = nlohmann::json::array();
    for (size_t i = 0; i < std::min(topK, all.size()); ++i) {
        nlohmann::json item;
        item["distance"] = all[i].dist;
        item["model_label"] = all[i].rec.value("model_label", "");
        if (all[i].rec.contains("snapshot")) item["snapshot"] = all[i].rec["snapshot"];
        ctx.push_back(std::move(item));
    }
    return ctx.dump();
}

void AnalysisMemory::saveCase(const std::string& modelLabel, const nlohmann::json& fingerprint,
                                const nlohmann::json& mergedReportSnapshot) {
    fs::create_directories(dir_);
    auto name = std::string("case_") +
                  std::to_string(
                      std::chrono::system_clock::now().time_since_epoch().count());
    for (auto& c : name)
        if (c == ':' || c == '.') c = '_';
    fs::path path = fs::path(dir_) / (name + ".json");

    nlohmann::json rec;
    rec["model_label"] = modelLabel;
    rec["fingerprint"] = fingerprint;
    rec["snapshot"] = mergedReportSnapshot;

    std::ofstream out(path);
    out << rec.dump(2);
}

} // namespace physisim::analysis
