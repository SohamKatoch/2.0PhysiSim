#include "core/Application.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

#include <GLFW/glfw3.h>

#include <imgui.h>

#include "ai/AnalysisClient.h"
#include "ai/AIOrchestrator.h"
#include "ai/OptimizerCommands.h"
#include "ai/CommandValidator.h"
#include "analysis/AnalysisMemory.h"
#include "analysis/DefectDetector.h"
#include "analysis/SimulationInsightPack.h"
#include "analysis/GeometryAnalyzer.h"
#include "analysis/MeshBenchmark.h"
#include "analysis/MeshHighlightMerge.h"
#include "analysis/TriangleWeakness.h"
#include "analysis/WeaknessField.h"
#include "core/CommandSystem.h"
#include "core/Scene.h"
#include "fea/GpuLaplacianSmooth.h"
#include "fem/calculix/FemCalculix.h"
#include "fem/FemCompare.h"
#include "fem/FemMeshReadiness.h"
#include "fem/FemTypes.h"
#include "fem/TetrahedralMesh.h"
#include "geometry/GeometryEngine.h"
#include "geometry/Mesh.h"
#include "rendering/Camera.h"
#include "rendering/RayPick.h"
#include "ipc/CommandApiServer.h"
#include "platform/FileDialog.h"
#include "rendering/MeshVizPost.h"
#include "rendering/VulkanRenderer.h"
#include "sim/MassSpringSystem.h"
#include "sim/SimulationScenario.h"
#include "ui/ImGuiLayer.h"

namespace physisim::core {

namespace {

struct InsightCache {
    /// Per-triangle state after deterministic + stress merge (before kinematic scenario weights).
    std::vector<analysis::TriangleWeakness> triScenarioSource;
    /// AI-merged scalar severity (same ordering as mesh triangles).
    std::vector<float> triWeakness;
    std::vector<float> triStress;
    std::vector<float> triPropagated;
    /// Mass–spring strain stress [0,1] per triangle (same order as mesh.indices/3).
    std::vector<float> triStrainStress;
    /// Positions captured at last successful Run analysis (restore after physics preview).
    std::vector<glm::vec3> meshRestPositions;
    nlohmann::json merged;
    std::unordered_map<uint64_t, uint8_t> edgeFaceCount;
    float meshUnitToMm{1.f};
    bool valid{false};
};

static constexpr int kNumSimMaterialPresets = 3;
static const sim::SimMaterial kSimMaterialPresets[kNumSimMaterialPresets] = {
    {200e9f, 7850.f, 0.02f},
    {69e9f, 2700.f, 0.03f},
    {2e6f, 920.f, 0.35f},
};

static const char* kSimMaterialPackIds[kNumSimMaterialPresets] = {"mild_steel", "aluminum", "rubber_like"};

static std::string scenarioLabelString(sim::ScenarioType t) {
    switch (t) {
    case sim::ScenarioType::Highway:
        return "highway";
    case sim::ScenarioType::Braking:
        return "braking";
    case sim::ScenarioType::Cornering:
        return "cornering";
    case sim::ScenarioType::Bump:
        return "bump";
    }
    return "unknown";
}

/// Deterministic analysis refresh (no Model 2 call) after geometry edits in the optimizer loop.
static void refreshInsightDeterministic(core::Scene& scene, const std::string& activeId, InsightCache& meshInsight,
                                        float meshUnitToMm, float densityKg, analysis::DefectDetector& defectDetector,
                                        bool physicsSimEnabled, sim::MassSpringSystem& massSpring,
                                        const sim::MassSpringParams& springParams, bool constraintsEnabled,
                                        std::string& commandLog) {
    auto* node = scene.find(activeId);
    if (!node || !node->mesh) return;
    analysis::DefectDetectorOptions opts;
    opts.useAi = false;
    opts.feedbackLoop = false;
    opts.useRag = false;
    opts.persistCase = false;
    opts.meshUnitToMm = meshUnitToMm;
    opts.materialDensityKgPerM3 = densityKg;
    opts.caseLabel = activeId;
    auto rep = defectDetector.evaluate(*node->mesh, opts);
    auto det = analysis::GeometryAnalyzer::analyze(*node->mesh);
    std::vector<uint32_t> hi;
    std::vector<float> hw;
    std::vector<float> triMerged;
    std::vector<analysis::TriangleWeakness> mergedState;
    analysis::buildMergedViewportHighlights(det, rep.triangleStressProxy, rep.merged, hi, hw, &triMerged,
                                              &mergedState);
    meshInsight.triScenarioSource = std::move(mergedState);
    meshInsight.triWeakness = std::move(triMerged);
    meshInsight.triStress.resize(meshInsight.triScenarioSource.size());
    for (size_t ti = 0; ti < meshInsight.triStress.size(); ++ti)
        meshInsight.triStress[ti] = meshInsight.triScenarioSource[ti].stressProxy;
    meshInsight.merged = rep.merged;
    meshInsight.meshUnitToMm = meshUnitToMm;
    meshInsight.meshRestPositions = node->mesh->positions;
    meshInsight.triStrainStress.clear();
    rendering::buildMeshEdgeFaceCounts(*node->mesh, meshInsight.edgeFaceCount);
    meshInsight.valid = !meshInsight.triWeakness.empty();
    if (physicsSimEnabled && meshInsight.valid) {
        if (!massSpring.build(*node->mesh, meshInsight.triScenarioSource, springParams, meshInsight.meshUnitToMm,
                              constraintsEnabled))
            commandLog += "[model2] Mass-spring rebuild failed after deterministic refresh.\n";
    }
}

static bool sameMaterial(const sim::SimMaterial& a, const sim::SimMaterial& b) {
    return a.youngsModulusPa == b.youngsModulusPa && a.densityKgM3 == b.densityKgM3 && a.maxStrain == b.maxStrain;
}

static float maxTriScalar01(const std::vector<float>& v) {
    float m = 0.f;
    for (float x : v) m = std::max(m, std::clamp(x, 0.f, 1.f));
    return m;
}

static uint64_t edgeKeyUndirected(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
}

static float triangleMinEdgeMm(const geometry::Mesh& mesh, uint32_t tri, float mmPerUnit) {
    size_t b = static_cast<size_t>(tri) * 3;
    if (b + 2 >= mesh.indices.size()) return 0.f;
    uint32_t ia = mesh.indices[b], ib = mesh.indices[b + 1], ic = mesh.indices[b + 2];
    if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size()) return 0.f;
    glm::vec3 p0 = mesh.positions[ia], p1 = mesh.positions[ib], p2 = mesh.positions[ic];
    float e0 = glm::distance(p0, p1), e1 = glm::distance(p1, p2), e2 = glm::distance(p2, p0);
    return std::min({e0, e1, e2}) * mmPerUnit;
}

static bool triangleHasBoundaryEdge(const InsightCache& c, const geometry::Mesh& mesh, uint32_t tri) {
    if (!c.valid) return false;
    size_t b = static_cast<size_t>(tri) * 3;
    if (b + 2 >= mesh.indices.size()) return false;
    uint32_t ia = mesh.indices[b], ib = mesh.indices[b + 1], ic = mesh.indices[b + 2];
    auto isBoundary = [&](uint32_t u, uint32_t v) {
        auto it = c.edgeFaceCount.find(edgeKeyUndirected(u, v));
        if (it == c.edgeFaceCount.end()) return false;
        return it->second == 1;
    };
    return isBoundary(ia, ib) || isBoundary(ib, ic) || isBoundary(ic, ia);
}

static const char* severityBandLabel(float weakness01) {
    if (weakness01 < 0.22f) return "Low (cool)";
    if (weakness01 < 0.45f) return "Moderate";
    if (weakness01 < 0.68f) return "Elevated";
    return "Severe (hot)";
}

static std::string faceSuggestionText(const nlohmann::json& merged, float weakness01, float stress01) {
    if (merged.contains("design_actions") && merged["design_actions"].is_array()) {
        std::string s;
        for (const auto& a : merged["design_actions"]) {
            if (!a.is_object()) continue;
            if (a.contains("rationale") && a["rationale"].is_string()) {
                s += a["rationale"].get<std::string>();
                s += ' ';
            }
            if (s.size() > 400) break;
        }
        if (!s.empty()) return s;
    }
    if (weakness01 > 0.5f)
        return "Engine proxy flags this region (thin/topology); consider thickening or fillets before "
               "light-weighting.";
    if (stress01 > 0.55f)
        return "High Laplacian proxy — check curvature or load path; validate with real FEA if critical.";
    return "Run Analysis (optionally with Ollama) for structured design_actions tied to this mesh.";
}

static bool endsWithStl(const std::string& p) {
    if (p.size() < 4) return false;
    std::string ext = p.substr(p.size() - 4);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".stl";
}

static void drawPhysiSimMainMenuBar(GLFWwindow* window, std::string& commandLog) {
    if (!ImGui::BeginMainMenuBar()) return;
    ImGui::TextUnformatted("PhysiSim");
    ImGui::Separator();
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset window layout hint")) {
            commandLog +=
                "[ui] To reset panel positions: quit, delete imgui.ini next to the executable (or cwd), relaunch.\n";
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Esc")) glfwSetWindowShouldClose(window, GLFW_TRUE);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Orbit — hold right mouse, drag", nullptr, false, false);
        ImGui::MenuItem("Zoom — scroll or = / -", nullptr, false, false);
        ImGui::MenuItem("Load — drag .stl onto window", nullptr, false, false);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

static bool applyAnalyzeFem(Scene& scene, const Command& cmd, std::string& commandLog) {
    const auto& p = cmd.parameters;
    std::string solver = p.value("solver", std::string(""));
    if (solver != "calculix") {
        commandLog += "[fem] analyze_fem requires parameters.solver = \"calculix\"\n";
        return false;
    }

    bool skipReadiness = p.contains("fem_skip_readiness") && p["fem_skip_readiness"].is_boolean() &&
                         p["fem_skip_readiness"].get<bool>();
    bool demoMesh = p.contains("demo_mesh") && p["demo_mesh"].is_boolean() && p["demo_mesh"].get<bool>();
    std::string storeIdPre = scene.activeModelId();
    if (cmd.target) storeIdPre = *cmd.target;

    if (!skipReadiness && !demoMesh) {
        SceneNode* preNode = scene.find(storeIdPre);
        fem::FemReadinessReport pre;
        if (!preNode || !preNode->mesh || preNode->mesh->indices.empty()) {
            pre.readiness = fem::MeshFEMReadiness::NEEDS_REPAIR;
            pre.issues.push_back({"no_surface_mesh",
                                  "Target model has no triangle surface mesh to run FEM preflight on.", "repair"});
            pre.suggestions.push_back("Load an STL or ensure the model has a surface mesh before analyze_fem.");
        } else {
            pre = fem::evaluateFEMReadiness(*preNode->mesh);
        }

        bool allowRepair = p.contains("fem_allow_needs_repair") && p["fem_allow_needs_repair"].is_boolean() &&
                           p["fem_allow_needs_repair"].get<bool>();

        auto logPreflight = [&]() {
            commandLog += "[fem] preflight (" + std::string(pre.toJson()["status"].get<std::string>()) + "): ";
            for (const auto& is : pre.issues) commandLog += is.code + "; ";
            commandLog += "\n";
            commandLog += pre.toJson().dump(2);
            commandLog += "\n";
        };

        if (pre.readiness == fem::MeshFEMReadiness::INVALID) {
            logPreflight();
            commandLog += "[fem] analyze_fem blocked: surface mesh is INVALID for FEM (fix topology or intersections).\n";
            return false;
        }
        if (pre.readiness == fem::MeshFEMReadiness::NEEDS_REPAIR) {
            if (!allowRepair) {
                logPreflight();
                commandLog += "[fem] analyze_fem blocked: surface NEEDS_REPAIR. Set "
                              "\"fem_allow_needs_repair\":true to override (not recommended).\n";
                return false;
            }
            logPreflight();
            commandLog += "[fem] preflight warning: proceeding with fem_allow_needs_repair override.\n";
        } else {
            commandLog += "[fem] preflight: ok (surface mesh READY for FEM).\n";
        }
    } else if (demoMesh && !skipReadiness) {
        commandLog += "[fem] preflight skipped (demo_mesh uses built-in tetrahedral test geometry).\n";
    }

    fem::FemInput fin;
    if (p.contains("ccx_executable") && p["ccx_executable"].is_string())
        fin.ccxExecutable = p["ccx_executable"].get<std::string>();
    if (p.contains("work_directory") && p["work_directory"].is_string())
        fin.workDirectory = p["work_directory"].get<std::string>();
    if (p.contains("job_name") && p["job_name"].is_string()) fin.jobName = p["job_name"].get<std::string>();
    if (p.contains("young_modulus_mpa") && p["young_modulus_mpa"].is_number())
        fin.youngModulusMpa = p["young_modulus_mpa"].get<double>();
    if (p.contains("poisson_ratio") && p["poisson_ratio"].is_number())
        fin.poissonRatio = p["poisson_ratio"].get<double>();
    if (p.contains("density_kg_m3") && p["density_kg_m3"].is_number())
        fin.densityKgM3 = p["density_kg_m3"].get<double>();
    if (p.contains("enable_gravity") && p["enable_gravity"].is_boolean())
        fin.enableGravity = p["enable_gravity"].get<bool>();
    if (p.contains("keep_work_files") && p["keep_work_files"].is_boolean())
        fin.keepWorkFiles = p["keep_work_files"].get<bool>();

    fem::TetrahedralMesh mesh;
    std::string storeId = scene.activeModelId();
    if (cmd.target) storeId = *cmd.target;

    if (demoMesh) {
        mesh = fem::TetrahedralMesh::singleCornerTetFromUnitCube();
    } else {
        SceneNode* node = scene.find(storeId);
        if (!node || !node->volumeMesh || node->volumeMesh->empty()) {
            commandLog += "[fem] No volume mesh on target model; use create with "
                          "\"attach_fem_demo_volume\":true or \"demo_mesh\":true in analyze_fem\n";
            return false;
        }
        mesh = *node->volumeMesh;
    }

    if (p.contains("fixed_nodes") && p["fixed_nodes"].is_array()) {
        fin.fixedNodes.clear();
        for (const auto& x : p["fixed_nodes"]) {
            if (x.is_number_integer()) fin.fixedNodes.push_back(x.get<uint32_t>());
        }
    } else if (demoMesh) {
        fin.fixedNodes = {0, 1, 2};
        fin.loadNode = 3;
        fin.loadForceN = glm::dvec3(0.0, -100.0, 0.0);
    }
    if (p.contains("load") && p["load"].is_object()) {
        const auto& L = p["load"];
        if (L.contains("node") && L["node"].is_number_integer()) fin.loadNode = L["node"].get<uint32_t>();
        if (L.contains("f") && L["f"].is_array() && L["f"].size() >= 3) {
            fin.loadForceN = glm::dvec3(L["f"][0].get<double>(), L["f"][1].get<double>(),
                                        L["f"][2].get<double>());
        }
    }

    std::string err;
    fem::FemResult res = fem::runCalculix(mesh, fin, err);
    if (!res.ok) {
        commandLog += "[fem] " + err + "\n";
        if (!res.diagnosticLog.empty()) commandLog += res.diagnosticLog + "\n";
        return false;
    }

    if (SceneNode* n = scene.find(storeId)) n->lastFemResult = std::move(res);

    if (p.contains("compare_with_placeholder") && p["compare_with_placeholder"].is_boolean() &&
        p["compare_with_placeholder"].get<bool>()) {
        fem::FemResult ph = fem::makePlaceholderInternalResult(mesh);
        if (SceneNode* n = scene.find(storeId); n && n->lastFemResult) {
            fem::ComparisonResult cmp = fem::compareSolvers(ph, *n->lastFemResult);
            commandLog += "[fem] compare vs placeholder: disp_L2_rel_%=" +
                          std::to_string(cmp.displacementRelativeL2Percent) +
                          " max_dsigma=" + std::to_string(cmp.maxVonMisesDifference) +
                          " max_rel_sigma_%=" + std::to_string(cmp.maxVonMisesRelativePercent) + "\n";
        }
    }

    commandLog += "[ok] analyze_fem " + cmd.toJson().dump() + "\n";
    return true;
}

static void exportActiveStl(const Scene& scene, const std::string& path, std::string& commandLog) {
    if (path.empty()) return;
    auto* node = scene.find(scene.activeModelId());
    if (!node || !node->mesh) {
        commandLog += "[export] No active mesh.\n";
        return;
    }
    std::string e;
    if (!node->mesh->saveStlBinary(path, e))
        commandLog += "[export] " + e + "\n";
    else
        commandLog += "[export] Wrote binary STL: " + path + "\n";
}

static void loadStlIntoScene(Scene& scene, const std::string& path, std::string& commandLog, bool& meshDirty,
                             std::vector<char>& stlBufDisplay, InsightCache* insightCache) {
    if (path.empty()) return;
    if (!endsWithStl(path)) {
        commandLog += "[stl] Expected a .stl file: " + path + "\n";
        return;
    }
    geometry::Mesh m;
    std::string le;
    if (!geometry::Mesh::loadStl(path, m, le)) {
        commandLog += "[stl] " + le + " (" + path + ")\n";
        return;
    }
    std::string id = std::filesystem::path(path).stem().string();
    if (id.empty()) id = "imported";
    scene.addOrReplace(id, std::make_shared<geometry::Mesh>(std::move(m)));
    scene.activeModelId() = id;
    meshDirty = true;
    if (insightCache) insightCache->valid = false;
    commandLog += "[stl] Loaded: " + path + " as '" + id + "'\n";
    std::strncpy(stlBufDisplay.data(), path.c_str(), stlBufDisplay.size() - 1);
    stlBufDisplay[stlBufDisplay.size() - 1] = '\0';
}

/// Packs multi-channel per-triangle weakness to vertices (vec4 + propagated scalar) for GPU heatmaps.
void applyPerTriangleWeaknessPackToVertices(geometry::Mesh& mesh, const std::vector<analysis::TriangleWeakness>& triState,
                                            const std::vector<float>& triPropagated01) {
    mesh.ensureHighlightBuffer();
    std::fill(mesh.defectHighlight.begin(), mesh.defectHighlight.end(), glm::vec4(0.f));
    std::fill(mesh.weaknessPropagated.begin(), mesh.weaknessPropagated.end(), 0.f);
    if (mesh.indices.empty() || triState.empty()) return;
    const size_t triCount = mesh.indices.size() / 3;
    if (triState.size() < triCount) return;
    const bool haveProp = triPropagated01.size() >= triCount;
    std::vector<glm::vec4> sum(mesh.positions.size(), glm::vec4(0.f));
    std::vector<float> sumP(mesh.positions.size(), 0.f);
    std::vector<uint32_t> count(mesh.positions.size(), 0u);
    for (size_t t = 0; t < triCount; ++t) {
        const analysis::TriangleWeakness& w = triState[t];
        float stressCh = std::max(w.stressProxy, w.strainStress);
        glm::vec4 pack(w.geoWeakness, stressCh, w.velocityWeight, w.loadWeight);
        pack = glm::clamp(pack, glm::vec4(0.f), glm::vec4(1.f));
        float pv = haveProp ? std::clamp(triPropagated01[t], 0.f, 1.f) : 0.f;
        const size_t b = t * 3;
        for (int k = 0; k < 3; ++k) {
            uint32_t vi = mesh.indices[b + static_cast<size_t>(k)];
            if (vi < sum.size()) {
                sum[vi] += pack;
                sumP[vi] += pv;
                ++count[vi];
            }
        }
    }
    for (size_t vi = 0; vi < mesh.defectHighlight.size(); ++vi) {
        if (count[vi] > 0) {
            float inv = 1.f / static_cast<float>(count[vi]);
            mesh.defectHighlight[vi] = sum[vi] * inv;
            mesh.weaknessPropagated[vi] = sumP[vi] * inv;
        }
    }
}

void refreshWeaknessVisualization(geometry::Mesh& mesh, InsightCache& insight,
                                  const sim::SimulationScenario& scenario) {
    if (!insight.valid || insight.triScenarioSource.empty()) return;
    std::vector<analysis::TriangleWeakness> tri = insight.triScenarioSource;
    const size_t triCount = mesh.indices.size() / 3;
    if (insight.triStrainStress.size() >= triCount) {
        for (size_t t = 0; t < triCount && t < tri.size(); ++t) tri[t].strainStress = insight.triStrainStress[t];
    }
    float sp = 0.f, ap = 0.f, cr = 0.f;
    sim::scenarioToVisualizationSliders(scenario, sp, ap, cr);
    analysis::applyKinematicWeaknessProxies(tri, sp, ap, cr);
    insight.triPropagated.resize(insight.triWeakness.size());
    for (size_t t = 0; t < insight.triPropagated.size(); ++t)
        insight.triPropagated[t] =
            (t < insight.triStrainStress.size()) ? insight.triStrainStress[t] : 0.f;
    applyPerTriangleWeaknessPackToVertices(mesh, tri, insight.triPropagated);
}

struct WindowUserData {
    Application* app{nullptr};
    rendering::VulkanRenderer* vk{nullptr};
};

static uint16_t parseIpcPort(int argc, char** argv) {
    auto parseU16 = [](const char* s) -> uint16_t {
        try {
            int v = std::stoi(s);
            if (v <= 0 || v > 65535) return 0;
            return static_cast<uint16_t>(v);
        } catch (...) {
            return 0;
        }
    };
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--ipc-port" && i + 1 < argc) return parseU16(argv[++i]);
        if (a.size() > 12 && a.substr(0, 12) == "--ipc-port=") return parseU16(std::string(a.substr(12)).c_str());
    }
    if (const char* e = std::getenv("PHYSISIM_IPC_PORT")) return parseU16(e);
    return 0;
}

/// Bind address for the HTTP API (127.0.0.1 default; use 0.0.0.0 in Docker with port publish).
static std::string parseIpcListenHost(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--ipc-host" && i + 1 < argc) return std::string(argv[++i]);
        if (a.size() > 12 && a.substr(0, 12) == "--ipc-host=") return std::string(a.substr(12));
    }
    if (const char* e = std::getenv("PHYSISIM_IPC_HOST")) return std::string(e);
    return "127.0.0.1";
}

static void printHelp() {
    std::printf(
        "PhysiSim CAD\n"
        "  --ipc-port <n>     HTTP JSON API on --ipc-host:n (default host 127.0.0.1)\n"
        "  PHYSISIM_IPC_PORT  same if --ipc-port not passed\n"
        "  --ipc-host <addr>  bind address (default 127.0.0.1; 0.0.0.0 = all interfaces)\n"
        "  PHYSISIM_IPC_HOST  same if --ipc-host not passed\n"
        "  --help             this message\n");
}

} // namespace

int Application::run(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--help") {
            printHelp();
            return 0;
        }
    }

    const uint16_t ipcPort = parseIpcPort(argc, argv);
    const std::string ipcListenHost = parseIpcListenHost(argc, argv);

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    int w = 1280, h = 720;
    GLFWwindow* window = glfwCreateWindow(w, h, "PhysiSim", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    rendering::VulkanRenderer vk;
    std::string err;
    if (!vk.init(window, w, h, err)) {
        std::fprintf(stderr, "Vulkan init: %s\n", err.c_str());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    ui::ImGuiLayer imgui;
    if (!imgui.init(window, vk, err)) {
        std::fprintf(stderr, "ImGui init: %s\n", err.c_str());
        vk.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    WindowUserData ud{this, &vk};
    glfwSetWindowUserPointer(window, &ud);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int ww, int hh) {
        auto* u = static_cast<WindowUserData*>(glfwGetWindowUserPointer(win));
        if (u && u->vk) u->vk->resize(ww, hh);
        if (u && u->app) u->app->markMeshDirty();
    });
    glfwSetDropCallback(window, [](GLFWwindow* win, int count, const char** paths) {
        if (count < 1 || !paths || !paths[0]) return;
        auto* u = static_cast<WindowUserData*>(glfwGetWindowUserPointer(win));
        if (u && u->app) u->app->queueStlPath(std::string(paths[0]));
    });
    glfwSetScrollCallback(window, [](GLFWwindow* win, double, double yoff) {
        auto* u = static_cast<WindowUserData*>(glfwGetWindowUserPointer(win));
        if (u && u->app) u->app->addScrollDelta(static_cast<float>(yoff));
    });

    Scene scene;
    scene.addOrReplace("demo", std::make_shared<geometry::Mesh>(geometry::Mesh::createUnitCube()));

    CommandSystem commands;
    geometry::GeometryEngine geoEngine(scene);

    commands.setHandler([&](const Command& cmd) -> bool {
        std::string e;
        if (!ai::CommandValidator::validate(cmd, e)) {
            commandLog_ += "[reject] " + e + "\n";
            return false;
        }
        if (cmd.action == CommandAction::AnalyzeFem) {
            if (!applyAnalyzeFem(scene, cmd, commandLog_)) return false;
            return true;
        }
        if (!geoEngine.apply(cmd, e)) {
            commandLog_ += "[fail] " + e + "\n";
            return false;
        }
        commandLog_ += "[ok] " + cmd.toJson().dump() + "\n";
        meshDirty_ = true;
        return true;
    });

    std::string ipcSceneJson = R"({"active":"","ids":[]})";
    std::vector<uint8_t> ipcStlBlob;
    std::mutex ipcApiMu;
    std::unique_ptr<ipc::CommandApiServer> ipcServer;
    if (ipcPort > 0) {
        ipcServer = std::make_unique<ipc::CommandApiServer>();
        ipcServer->start(
            ipcPort,
            [&ipcSceneJson, &ipcApiMu] {
                std::lock_guard<std::mutex> lk(ipcApiMu);
                return ipcSceneJson;
            },
            [&ipcStlBlob, &ipcApiMu] {
                std::lock_guard<std::mutex> lk(ipcApiMu);
                return ipcStlBlob;
            },
            ipcListenHost);
        std::fprintf(stderr,
                     "[ipc] HTTP API http://%s:%u/  (GET /v1/health, /v1/scene, /v1/mesh/stl, ...)\n",
                     ipcListenHost.c_str(), static_cast<unsigned>(ipcPort));
    }

    std::unique_ptr<fea::GpuLaplacianSmooth> gpuLaplacian;
    {
        std::string fe;
        auto lap = std::make_unique<fea::GpuLaplacianSmooth>();
        if (lap->init(vk.physical(), vk.device(), vk.graphicsQueue(), vk.graphicsFamily(), fe))
            gpuLaplacian = std::move(lap);
        else
            commandLog_ += "[fea] GPU compute (Laplacian) init failed: " + fe + "\n";
    }

    static float laplacianLambda = 0.25f;
    static float weaknessStressScale = 1.f;
    static float weaknessVelocityScale = 1.f;
    static float weaknessLoadScale = 1.f;
    static float weaknessTimeMix = 0.f;
    static float weaknessVisualMode = 0.f;
    static bool vizDynamicNormalization = true;
    static int vizSmoothPasses = 2;
    static float vizSmoothLambda = 0.5f;
    static bool vizStrainAlert = true;
    static float vizStrainThreshold = 0.7f;
    static bool vizStrainBlink = false;
    static float vizDirectionWeight = 0.35f;
    static float deformExaggeration = 8.f;
    static char analysisModelBuf[160] = "llama3.1:8b";
    static char objectiveJsonBuf[1536] =
        "{\"objective\":\"minimize_max_strain\",\"constraints\":{\"max_uniform_scale_ratio\":1.06,"
        "\"notes\":\"Small translate/scale steps only; engine validates.\"}}";
    static int model2Iterations = 3;
    static int model2MaxCommandsPerIter = 2;
    static float model2MinConfidence = 0.5f;
    static bool model2OptimizerRag = true;
    static sim::SimulationScenario simScenario{};
    static int scenarioTypeUi = 0;
    static int materialPresetUi = 0;
    static bool physicsSimEnabled = false;
    static bool physicsWasEnabledLastFrame = false;
    static bool constraintsEnabled = true;
    static bool constraintsEnabledPrev = true;
    static sim::MassSpringSystem massSpring;
    static sim::MassSpringParams springParams{};
    static double simScenarioTime = 0.0;
    static sim::SimulationScenario scenarioVizPrev{
        sim::ScenarioType::Highway, -1.f, -1.f, -1.f}; // sentinel; first frame refreshes viz
    static sim::SimMaterial springMaterialPrev = kSimMaterialPresets[0];
    static float springStiffnessPrev = -1.f;
    static float analysisMaterialDensityKgM3 = 7850.f;
    static bool enableMeshInsight = true;
    static uint32_t hoverTriIndex = UINT32_MAX;
    static uint32_t selectedTriIndex = UINT32_MAX;
    static bool showFaceInspector = false;
    static std::vector<char> faceBookmarkBuf(768, 0);
    InsightCache meshInsight;

    ai::AIOrchestrator orchestrator;
    analysis::DefectDetector defectDetector;
    defectDetector.setAnalysisModelName(analysisModelBuf);
    rendering::Camera camera;
    camera.setAspect(static_cast<float>(w) / static_cast<float>(h));

    std::vector<char> nlBuf(2048, 0);
    std::vector<char> stlBuf(1024, 0);
    std::vector<char> stlExportBuf(1024, 0);
    std::vector<char> benchmarkBaselineBuf(1024, 0);
    static char cmdBuf[2048] =
        "{\"action\":\"create\",\"operations\":[],\"parameters\":{\"primitive\":\"cube\",\"id\":\"demo\"}}";

    std::string uploadErr;
    vk.uploadMesh(*scene.find("demo")->mesh, uploadErr, nullptr);

    double lx = 0, ly = 0;
    glfwGetCursorPos(window, &lx, &ly);
    double lastPhysicsTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double nowPhy = glfwGetTime();
        float phyDt = static_cast<float>(nowPhy - lastPhysicsTime);
        lastPhysicsTime = nowPhy;
        phyDt = std::clamp(phyDt, 0.f, 0.05f);

        if (auto* pNode = scene.find(scene.activeModelId());
            pNode && pNode->mesh && meshInsight.valid && physicsSimEnabled && massSpring.valid()) {
            simScenarioTime += static_cast<double>(phyDt);
            massSpring.setParams(springParams);
            massSpring.setExternalAcceleration(
                sim::scenarioAccelerationSolver(simScenario, simScenarioTime, meshUnitToMm_));
            massSpring.step(phyDt);
            massSpring.applyPositionsToMesh(*pNode->mesh);
            pNode->mesh->recomputeNormals();
            massSpring.computeTriangleStrainStress01(*pNode->mesh, meshInsight.triStrainStress);
            refreshWeaknessVisualization(*pNode->mesh, meshInsight, simScenario);
            meshDirty_ = true;
        } else if (physicsWasEnabledLastFrame && !physicsSimEnabled) {
            if (auto* n = scene.find(scene.activeModelId()); n && n->mesh) {
                if (massSpring.valid())
                    massSpring.restoreRestGeometryToMesh(*n->mesh);
                else if (meshInsight.meshRestPositions.size() == n->mesh->positions.size())
                    n->mesh->positions = meshInsight.meshRestPositions;
                n->mesh->recomputeNormals();
                meshInsight.triStrainStress.clear();
                refreshWeaknessVisualization(*n->mesh, meshInsight, simScenario);
                meshDirty_ = true;
            }
        }
        physicsWasEnabledLastFrame = physicsSimEnabled;

        if (!meshInsight.valid && massSpring.valid()) massSpring.clear();

        if (ipcServer) {
            ipc::PendingOp iop;
            while (ipcServer->poll(iop)) {
                switch (iop.type) {
                    case ipc::PendingOpType::LoadStl:
                        loadStlIntoScene(scene, iop.payload, commandLog_, meshDirty_, stlBuf, &meshInsight);
                        break;
                    case ipc::PendingOpType::ExportStl:
                        exportActiveStl(scene, iop.payload, commandLog_);
                        break;
                    case ipc::PendingOpType::CommandJson: {
                        std::string e;
                        if (!commands.submitJson(iop.payload, e)) commandLog_ += "[ipc] " + e + "\n";
                        break;
                    }
                    case ipc::PendingOpType::SetActive:
                        if (scene.find(iop.payload)) {
                            scene.activeModelId() = iop.payload;
                            meshDirty_ = true;
                            commandLog_ += "[ipc] active model -> " + iop.payload + "\n";
                        } else {
                            commandLog_ += "[ipc] unknown model id: " + iop.payload + "\n";
                        }
                        break;
                }
            }
        }

        if (!pendingStlPath_.empty()) {
            std::string p = std::move(pendingStlPath_);
            pendingStlPath_.clear();
            loadStlIntoScene(scene, p, commandLog_, meshDirty_, stlBuf, &meshInsight);
        }
        if (std::abs(pendingScrollY_) > 1e-6f) {
            camera.orbit(0.f, 0.f, -pendingScrollY_ * 0.15f);
            pendingScrollY_ = 0.f;
        }

        int fw, fh;
        glfwGetFramebufferSize(window, &fw, &fh);
        camera.setAspect(static_cast<float>(fw) / static_cast<float>(fh));

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            double cx, cy;
            glfwGetCursorPos(window, &cx, &cy);
            camera.orbit(static_cast<float>(cx - lx) * 0.005f, static_cast<float>(cy - ly) * 0.005f, 0.f);
            lx = cx;
            ly = cy;
        } else {
            glfwGetCursorPos(window, &lx, &ly);
        }
        if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS) camera.orbit(0, 0, -0.05f);
        if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS) camera.orbit(0, 0, 0.05f);

        imgui.newFrame();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !ImGui::GetIO().WantCaptureKeyboard)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        drawPhysiSimMainMenuBar(window, commandLog_);

        const float menuPadY = 30.f;
        const float logBarH = 192.f;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(14.f, menuPadY), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(368.f, 278.f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Commands");
        if (ImGui::CollapsingHeader("About commands & Ollama", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "AI uses Ollama at 127.0.0.1:11434. Successful JSON updates the active mesh on the next GPU "
                "upload. Check Log for [ok] or errors. Run ollama serve and pull models (e.g. llama3.1:8b, "
                "qwen2.5-math:7b).");
        }
        ImGui::Separator();
        ImGui::InputTextMultiline("Natural language", nlBuf.data(), nlBuf.size(), ImVec2(-1, 80));
        if (ImGui::Button("Interpret + execute (LLM)")) {
            std::string cmdJson, e;
            std::string userText(nlBuf.data());
            if (!orchestrator.interpretUserIntent(userText, cmdJson, e))
                commandLog_ += "[ai] " + e + "\n";
            else if (!commands.submitJson(cmdJson, e))
                commandLog_ += "[cmd] " + e + "\n";
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Structured command (JSON):");
        ImGui::InputTextMultiline("##cmdjson", cmdBuf, sizeof(cmdBuf), ImVec2(-1, 100));
        if (ImGui::Button("Execute JSON")) {
            std::string e;
            if (!commands.submitJson(cmdBuf, e)) commandLog_ += "[cmd] " + e + "\n";
        }
        ImGui::End();

        const float sceneTop = menuPadY + 286.f;
        const float sceneBot = logBarH + 22.f;
        ImGui::SetNextWindowPos(ImVec2(14.f, sceneTop), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(368.f, std::max(200.f, ds.y - sceneTop - sceneBot)), ImGuiCond_FirstUseEver);
        ImGui::Begin("Scene");
        ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.f, 1.f), "Viewport");
        if (ImGui::CollapsingHeader("Navigation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("Orbit — hold right mouse, drag");
            ImGui::BulletText("Zoom — scroll or = / -");
            ImGui::BulletText("Quit — Esc (when a text field is not focused)");
        }
        if (ImGui::CollapsingHeader("STL units (mm)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Prefer millimeter STLs. Coordinates are kept as in the file. Use Analysis scale 1.0 so "
                "1 mesh unit = 1 mm. Paths are local disk only.");
        }
        ImGui::Separator();
        ImGui::Checkbox("Interactive mesh insight (hover / pick)", &enableMeshInsight);
        ImGui::BulletText("Hover: tooltip with face metrics (Ctrl: normal + boundary)");
        ImGui::BulletText("Left-click: face inspector; Shift+click: future apply-to-region");
        ImGui::Separator();
        ImGui::TextUnformatted("Import mesh (.stl)");
        ImGui::TextWrapped(
            "Drag and drop a .stl file anywhere on this window, or set the path below and click Load.");
#ifdef _WIN32
        if (ImGui::Button("Browse for STL...", ImVec2(-1, 0))) {
            if (auto p = platform::openStlFileDialog(window)) {
                std::strncpy(stlBuf.data(), p->c_str(), stlBuf.size() - 1);
                stlBuf[stlBuf.size() - 1] = '\0';
            }
        }
#else
        ImGui::TextDisabled("Browse: use path field or drag-drop (native dialog is Windows-only).");
#endif
        ImGui::InputText("Path##stl", stlBuf.data(), stlBuf.size());
        if (ImGui::Button("Load from path", ImVec2(-1, 0)))
            loadStlIntoScene(scene, std::string(stlBuf.data()), commandLog_, meshDirty_, stlBuf, &meshInsight);
        ImGui::Separator();
        ImGui::Text("Models in scene");
        const char* active = scene.activeModelId().empty() ? "(none)" : scene.activeModelId().c_str();
        ImGui::Text("Active: %s", active);
        for (const auto& id : scene.ids()) {
            if (ImGui::Selectable(id.c_str(), id == scene.activeModelId())) scene.activeModelId() = id;
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Export active mesh (binary STL)");
        ImGui::TextWrapped(
            "Writes triangle soup in model units (mm if you imported mm STLs and did not rescale). "
            "Does not bake scene transform: export uses raw mesh vertices. "
            "For an AI-assisted / smoothed variant: run mesh ops or GPU Laplacian here, then export — "
            "the file is the current CPU mesh only.");
#ifdef _WIN32
        if (ImGui::Button("Browse save location...", ImVec2(-1, 0))) {
            if (auto p = platform::saveStlFileDialog(window)) {
                std::strncpy(stlExportBuf.data(), p->c_str(), stlExportBuf.size() - 1);
                stlExportBuf[stlExportBuf.size() - 1] = '\0';
            }
        }
#else
        ImGui::TextDisabled("Save dialog: Windows only; type export path below.");
#endif
        ImGui::InputText("Export path##stlout", stlExportBuf.data(), stlExportBuf.size());
        if (ImGui::Button("Export binary STL", ImVec2(-1, 0)))
            exportActiveStl(scene, std::string(stlExportBuf.data()), commandLog_);
        ImGui::Separator();
        ImGui::TextUnformatted("GPU FEA (experimental)");
        ImGui::TextWrapped(
            "Vulkan compute: one Jacobi Laplacian smoothing step toward neighbor centroids (early GPU FEA hook). "
            "Results copy back to CPU; large meshes may hitch briefly.");
        ImGui::SliderFloat("Smooth blend (lambda)", &laplacianLambda, 0.02f, 1.f, "%.2f");
        if (!gpuLaplacian || !gpuLaplacian->ready()) ImGui::BeginDisabled();
        if (ImGui::Button("Run GPU Laplacian (1 step)", ImVec2(-1, 0))) {
            auto* node = scene.find(scene.activeModelId());
            if (!node || !node->mesh)
                commandLog_ += "[fea] No active mesh.\n";
            else {
                std::string e;
                if (gpuLaplacian && gpuLaplacian->smoothStep(*node->mesh, laplacianLambda, e)) {
                    meshInsight.valid = false;
                    meshDirty_ = true;
                    commandLog_ += "[fea] GPU Laplacian step OK\n";
                } else
                    commandLog_ += "[fea] " + e + "\n";
            }
        }
        if (!gpuLaplacian || !gpuLaplacian->ready()) ImGui::EndDisabled();
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(396.f, menuPadY), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(360.f, ds.x - 412.f), std::max(320.f, ds.y - menuPadY - logBarH - 28.f)),
                                 ImGuiCond_FirstUseEver);
        ImGui::Begin("Analysis");
        ImGui::Checkbox("Use AI defect layer (Ollama)", &useAiAnalysis_);
        ImGui::Checkbox("Phase 1: feedback loop (refine vs engine)", &analysisFeedbackLoop_);
        ImGui::Checkbox("Phase 2: RAG — retrieve similar cases in prompt", &analysisUseRag_);
        ImGui::Checkbox("Phase 2: save case to analysis_memory/", &analysisPersistCase_);
        ImGui::TextUnformatted("Manual mm scale (analysis / ground_truth only)");
        ImGui::DragFloat("mm per 1 mesh unit", &meshUnitToMm_, 0.01f, 0.001f, 1000.f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("For typical mm STLs: leave at 1.0. Change only if your file uses other units.");
        ImGui::DragFloat("Material density (kg/m³)", &analysisMaterialDensityKgM3, 1.f, 0.f, 20000.f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 = omit mass_kg in metrics. Default ~7850 is mild steel (order-of-magnitude).");
        if (ImGui::Button("Run analysis")) {
            auto* node = scene.find(scene.activeModelId());
            if (!node || !node->mesh)
                analysisText_ = "No active mesh.";
            else {
                if (meshInsight.meshRestPositions.size() == node->mesh->positions.size())
                    node->mesh->positions = meshInsight.meshRestPositions;
                analysis::DefectDetectorOptions opts;
                opts.useAi = useAiAnalysis_;
                opts.feedbackLoop = analysisFeedbackLoop_ && useAiAnalysis_;
                opts.useRag = analysisUseRag_;
                opts.persistCase = analysisPersistCase_;
                opts.meshUnitToMm = meshUnitToMm_;
                opts.materialDensityKgPerM3 = analysisMaterialDensityKgM3;
                opts.caseLabel = scene.activeModelId();
                auto rep = defectDetector.evaluate(*node->mesh, opts);
                analysisText_ = rep.merged.dump(2);
                auto det = analysis::GeometryAnalyzer::analyze(*node->mesh);
                std::vector<uint32_t> hi;
                std::vector<float> hw;
                std::vector<float> triMerged;
                std::vector<analysis::TriangleWeakness> mergedState;
                analysis::buildMergedViewportHighlights(det, rep.triangleStressProxy, rep.merged, hi, hw, &triMerged,
                                                        &mergedState);
                meshInsight.triScenarioSource = std::move(mergedState);
                meshInsight.triWeakness = std::move(triMerged);
                meshInsight.triStress.resize(meshInsight.triScenarioSource.size());
                for (size_t ti = 0; ti < meshInsight.triStress.size(); ++ti)
                    meshInsight.triStress[ti] = meshInsight.triScenarioSource[ti].stressProxy;
                meshInsight.merged = rep.merged;
                meshInsight.meshUnitToMm = meshUnitToMm_;
                meshInsight.meshRestPositions = node->mesh->positions;
                meshInsight.triStrainStress.clear();
                rendering::buildMeshEdgeFaceCounts(*node->mesh, meshInsight.edgeFaceCount);
                meshInsight.valid = !meshInsight.triWeakness.empty();
                simScenarioTime = 0.0;
                scenarioVizPrev = sim::SimulationScenario{sim::ScenarioType::Highway, -1.f, -1.f, -1.f};
                springStiffnessPrev = springParams.baseStiffness;
                constraintsEnabledPrev = constraintsEnabled;
                springMaterialPrev = springParams.material;
                massSpring.clear();
                refreshWeaknessVisualization(*node->mesh, meshInsight, simScenario);
                if (physicsSimEnabled &&
                    !massSpring.build(*node->mesh, meshInsight.triScenarioSource, springParams, meshInsight.meshUnitToMm,
                                      constraintsEnabled))
                    commandLog_ += "[sim] Mass-spring build failed (empty or degenerate edges).\n";
                meshDirty_ = true;
            }
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Model 2 — design optimizer (commands only)");
        ImGui::TextWrapped(
            "Loop: pack engine metrics → Ollama (analysis model) → JSON proposals → validated PhysiSim commands → "
            "apply → deterministic re-analysis. Engine stays authoritative; AI never overwrites physics numbers.");
        ImGui::InputText("Analysis model (Ollama)##m2", analysisModelBuf, sizeof(analysisModelBuf));
        if (ImGui::Button("Apply model name to detector##m2")) defectDetector.setAnalysisModelName(analysisModelBuf);
        ImGui::InputTextMultiline("Objective JSON##m2", objectiveJsonBuf, sizeof(objectiveJsonBuf), ImVec2(-1, 72));
        ImGui::SliderInt("Iterations##m2", &model2Iterations, 1, 5);
        ImGui::SliderInt("Max commands / iteration##m2", &model2MaxCommandsPerIter, 1, 4);
        ImGui::SliderFloat("Min confidence##m2", &model2MinConfidence, 0.f, 1.f, "%.2f");
        ImGui::Checkbox("Inject RAG (analysis_memory)##m2", &model2OptimizerRag);
        if (ImGui::Button("Run Model 2 optimizer loop##m2")) {
            auto* node = scene.find(scene.activeModelId());
            if (!node || !node->mesh)
                commandLog_ += "[model2] No active mesh.\n";
            else if (!meshInsight.valid)
                commandLog_ += "[model2] Run analysis first (need triangle insight cache).\n";
            else {
                defectDetector.setAnalysisModelName(analysisModelBuf);
                nlohmann::json lastMetricsAfter;
                nlohmann::json lastPackSnapshot;
                for (int iter = 0; iter < model2Iterations; ++iter) {
                    node = scene.find(scene.activeModelId());
                    if (!node || !node->mesh) break;
                    const size_t tc = node->mesh->indices.size() / 3;
                    std::vector<float> triStrain(tc, 0.f), triGeo(tc, 0.f);
                    for (size_t t = 0; t < tc; ++t) {
                        if (t < meshInsight.triStrainStress.size())
                            triStrain[t] = meshInsight.triStrainStress[t];
                        else if (t < meshInsight.triStress.size())
                            triStrain[t] = meshInsight.triStress[t];
                        if (t < meshInsight.triScenarioSource.size())
                            triGeo[t] = meshInsight.triScenarioSource[t].geoWeakness;
                    }
                    const int mid = std::clamp(materialPresetUi, 0, kNumSimMaterialPresets - 1);
                    nlohmann::json pack = analysis::buildModel2SimulationPack(
                        *node->mesh, triStrain, triGeo, meshInsight.meshUnitToMm, kSimMaterialPackIds[mid],
                        scenarioLabelString(simScenario.type), constraintsEnabled, 3.f);
                    lastPackSnapshot = pack;

                    std::string priorStr = "{}";
                    if (!lastMetricsAfter.empty()) priorStr = lastMetricsAfter.dump();

                    std::string rag;
                    if (model2OptimizerRag) {
                        analysis::AnalysisMemory mem("analysis_memory");
                        rag = mem.retrieveContextForPrompt(analysis::fingerprintFromSimulationPack(pack), 3);
                    }

                    ai::AnalysisClientConfig cfg;
                    cfg.host = "127.0.0.1";
                    cfg.port = 11434;
                    cfg.model = analysisModelBuf;
                    ai::AnalysisClient m2(cfg);
                    std::string raw, err;
                    if (!m2.proposeEngineCommands(pack.dump(), std::string(objectiveJsonBuf), iter, priorStr,
                                                  model2MaxCommandsPerIter + 2, model2MinConfidence * 0.75f, rag,
                                                  raw, err)) {
                        commandLog_ += "[model2] iter " + std::to_string(iter) + " " + err + "\n";
                        break;
                    }
                    std::vector<ai::EngineCommandProposal> props;
                    if (!ai::parseEngineProposals(raw, props, err)) {
                        commandLog_ += "[model2] parse: " + err + "\n";
                        break;
                    }
                    std::string filtLog;
                    ai::filterValidateProposals(props, model2MinConfidence,
                                                static_cast<size_t>(std::max(1, model2MaxCommandsPerIter)), filtLog);
                    commandLog_ += "[model2] iter " + std::to_string(iter) + " " + filtLog;
                    if (props.empty()) {
                        commandLog_ += "[model2] No proposals passed filters; stopping.\n";
                        break;
                    }
                    for (const auto& pr : props) {
                        std::string cmdLine = pr.command.dump();
                        std::string e;
                        if (!commands.submitJson(cmdLine, e))
                            commandLog_ += "[model2] apply failed: " + e + "\n";
                        else
                            commandLog_ += "[model2] applied: " + cmdLine + "\n";
                    }
                    refreshInsightDeterministic(scene, scene.activeModelId(), meshInsight, meshUnitToMm_,
                                                analysisMaterialDensityKgM3, defectDetector, physicsSimEnabled,
                                                massSpring, springParams, constraintsEnabled, commandLog_);
                    node = scene.find(scene.activeModelId());
                    if (node && node->mesh)
                        refreshWeaknessVisualization(*node->mesh, meshInsight, simScenario);
                    meshDirty_ = true;
                    simScenarioTime = 0.0;
                    lastMetricsAfter = nlohmann::json::object();
                    std::vector<float> ts2(tc, 0.f);
                    for (size_t t = 0; t < tc; ++t) {
                        if (t < meshInsight.triStrainStress.size()) ts2[t] = meshInsight.triStrainStress[t];
                        else if (t < meshInsight.triStress.size()) ts2[t] = meshInsight.triStress[t];
                    }
                    lastMetricsAfter["max_strain"] = maxTriScalar01(ts2);
                    double sumS = 0.0;
                    for (float x : ts2) sumS += static_cast<double>(x);
                    lastMetricsAfter["avg_strain"] =
                        tc ? static_cast<float>(sumS / static_cast<double>(tc)) : 0.f;
                    lastMetricsAfter["iteration"] = iter;
                }
                if (analysisPersistCase_ && !lastPackSnapshot.empty()) {
                    analysis::AnalysisMemory mem("analysis_memory");
                    nlohmann::json snap;
                    snap["optimizer"] = true;
                    snap["last_metrics"] = lastMetricsAfter;
                    mem.saveCase(scene.activeModelId() + "_model2",
                                 analysis::fingerprintFromSimulationPack(lastPackSnapshot), snap);
                }
                commandLog_ += "[model2] Loop finished.\n";
            }
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Defect heatmap (multi-channel)");
        if (!meshInsight.valid) ImGui::BeginDisabled();
        ImGui::SliderFloat("Stress scale##w", &weaknessStressScale, 0.f, 2.f, "%.2f");
        ImGui::SliderFloat("Velocity scale##w", &weaknessVelocityScale, 0.f, 2.f, "%.2f");
        ImGui::SliderFloat("Load scale##w", &weaknessLoadScale, 0.f, 2.f, "%.2f");
        ImGui::SliderFloat("Time mix (strain channel)##w", &weaknessTimeMix, 0.f, 1.f, "%.2f");
        ImGui::SliderFloat("Visual mode##w", &weaknessVisualMode, 0.f, 2.f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 = combined heat, 1 = RGB (geo / stress / load), 2 = multi-objective alignment");
        ImGui::Separator();
        ImGui::TextUnformatted("Heatmap display (visualization only)");
        if (ImGui::Checkbox("Dynamic min–max contrast##w", &vizDynamicNormalization)) meshDirty_ = true;
        if (ImGui::SliderInt("Viz smooth passes##w", &vizSmoothPasses, 0, 4)) meshDirty_ = true;
        if (ImGui::SliderFloat("Viz smooth blend##w", &vizSmoothLambda, 0.f, 1.f, "%.2f")) meshDirty_ = true;
        if (ImGui::Checkbox("Strain alert (≥ threshold)##w", &vizStrainAlert)) meshDirty_ = true;
        if (ImGui::SliderFloat("Alert threshold##w", &vizStrainThreshold, 0.3f, 0.95f, "%.2f")) meshDirty_ = true;
        if (ImGui::Checkbox("Blink alert##w", &vizStrainBlink)) meshDirty_ = true;
        if (ImGui::SliderFloat("Direction cue (displacement vs normal)##w", &vizDirectionWeight, 0.f, 1.f, "%.2f"))
            meshDirty_ = true;
        if (ImGui::SliderFloat("Deform exaggeration (×)##w", &deformExaggeration, 1.f, 25.f, "%.1f")) meshDirty_ = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scales rendered shape from analysis rest; physics mesh is unchanged.");
        ImGui::TextUnformatted("Simulation scenario (drives mass–spring loads + heatmap weights)");
        ImGui::Combo("Material##sim", &materialPresetUi, "Mild steel\0Aluminum\0Rubber-like\0\0");
        {
            static int lastMaterialPresetUi = -1;
            if (materialPresetUi != lastMaterialPresetUi) {
                springParams.material = kSimMaterialPresets[std::clamp(materialPresetUi, 0, kNumSimMaterialPresets - 1)];
                lastMaterialPresetUi = materialPresetUi;
            }
        }
        ImGui::Combo("Scenario type##sim", &scenarioTypeUi, "Highway\0Braking\0Cornering\0Bump\0\0");
        simScenario.type = static_cast<sim::ScenarioType>(std::clamp(scenarioTypeUi, 0, 3));
        ImGui::SliderFloat("Speed (mph)##sim", &simScenario.speed_mph, 0.f, 120.f, "%.0f");
        ImGui::SliderFloat("Intensity##sim", &simScenario.intensity, 0.f, 2.f, "%.2f");
        ImGui::SliderFloat("Duration (s)##sim", &simScenario.duration_s, 0.2f, 30.f, "%.1f");
        ImGui::Separator();
        ImGui::TextUnformatted("Mass–spring preview (CPU, non-FEA)");
        if (ImGui::Checkbox("Enable mass–spring##w", &physicsSimEnabled)) {
            if (physicsSimEnabled && meshInsight.valid) {
                if (auto* n = scene.find(scene.activeModelId()); n && n->mesh) {
                    if (meshInsight.meshRestPositions.size() == n->mesh->positions.size())
                        n->mesh->positions = meshInsight.meshRestPositions;
                    n->mesh->recomputeNormals();
                    if (!massSpring.build(*n->mesh, meshInsight.triScenarioSource, springParams, meshInsight.meshUnitToMm,
                                          constraintsEnabled))
                        commandLog_ += "[sim] Mass-spring build failed.\n";
                    else
                        commandLog_ += "[sim] Mass-spring system built from mesh edges.\n";
                    simScenarioTime = 0.0;
                    refreshWeaknessVisualization(*n->mesh, meshInsight, simScenario);
                    meshDirty_ = true;
                }
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Deforms the active mesh in CPU; strain/stress from springs drives the strain channel. "
                              "Run analysis first.");
        ImGui::Checkbox("Enable constraints (mount / boundary)##w", &constraintsEnabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pins open boundaries and heuristic mount points (partial Z on low-geo rim neighbors).");
        ImGui::SliderFloat("Spring stiffness##w", &springParams.baseStiffness, 5.f, 200.f, "%.0f");
        ImGui::SliderFloat("Damping##w", &springParams.damping, 0.f, 15.f, "%.1f");
        ImGui::SliderInt("Substeps / frame##w", &springParams.substepsPerFrame, 1, 16);
        ImGui::SliderFloat("Max strain (normalize to 1)##w", &springParams.material.maxStrain, 0.005f, 0.5f, "%.3f");
        ImGui::SliderFloat("Max displacement (0=off)##w", &springParams.maxDisplacement, 0.f, 0.5f, "%.3f");
        if (ImGui::Button("Reset simulation##w") && meshInsight.valid) {
            if (auto* n = scene.find(scene.activeModelId()); n && n->mesh) {
                simScenarioTime = 0.0;
                if (massSpring.valid())
                    massSpring.restoreRestGeometryToMesh(*n->mesh);
                else if (meshInsight.meshRestPositions.size() == n->mesh->positions.size())
                    n->mesh->positions = meshInsight.meshRestPositions;
                n->mesh->recomputeNormals();
                meshInsight.triStrainStress.clear();
                refreshWeaknessVisualization(*n->mesh, meshInsight, simScenario);
                meshDirty_ = true;
                commandLog_ += "[sim] Simulation reset (rest pose, velocities cleared).\n";
            }
        }
        if (ImGui::Button("Reset mesh to analysis rest##w") && meshInsight.valid) {
            if (auto* n = scene.find(scene.activeModelId()); n && n->mesh) {
                if (massSpring.valid())
                    massSpring.restoreRestGeometryToMesh(*n->mesh);
                else if (meshInsight.meshRestPositions.size() == n->mesh->positions.size())
                    n->mesh->positions = meshInsight.meshRestPositions;
                n->mesh->recomputeNormals();
                meshInsight.triStrainStress.clear();
                simScenarioTime = 0.0;
                refreshWeaknessVisualization(*n->mesh, meshInsight, simScenario);
                meshDirty_ = true;
                commandLog_ += "[sim] Mesh geometry reset to last analysis rest pose.\n";
            }
        }
        if (!meshInsight.valid) ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextUnformatted("Benchmark: original STL vs active mesh");
        ImGui::TextWrapped(
            "Loads a baseline file from disk and compares deterministic metrics (volume, area, mass, CoG shift, "
            "Laplacian proxy). Use after optimizing the active mesh (e.g. Laplacian) without replacing the baseline "
            "on disk.");
#ifdef _WIN32
        if (ImGui::Button("Browse baseline STL...", ImVec2(-1, 0))) {
            if (auto p = platform::openStlFileDialog(window)) {
                std::strncpy(benchmarkBaselineBuf.data(), p->c_str(), benchmarkBaselineBuf.size() - 1);
                benchmarkBaselineBuf[benchmarkBaselineBuf.size() - 1] = '\0';
            }
        }
#else
        ImGui::TextDisabled("Baseline browse: Windows only; type path below.");
#endif
        ImGui::InputText("Baseline path##bench", benchmarkBaselineBuf.data(), benchmarkBaselineBuf.size());
        if (ImGui::Button("Compare baseline vs active", ImVec2(-1, 0))) {
            auto* node = scene.find(scene.activeModelId());
            if (!node || !node->mesh)
                commandLog_ += "[bench] No active mesh.\n";
            else {
                std::string path(benchmarkBaselineBuf.data());
                geometry::Mesh baseline;
                std::string le;
                if (!geometry::Mesh::loadStl(path, baseline, le))
                    commandLog_ += "[bench] " + le + "\n";
                else {
                    analysis::MeshMetricsOptions mopt;
                    mopt.meshUnitMeters = meshUnitToMm_ * 1e-3f;
                    mopt.densityKgPerM3 = analysisMaterialDensityKgM3;
                    auto rep = analysis::benchmarkMeshPair(baseline, *node->mesh, mopt);
                    commandLog_ += "[bench]\n" + rep.dump(2) + "\n";
                }
            }
        }
        ImGui::TextUnformatted(analysisText_.c_str());
        ImGui::End();

        if (meshInsight.valid) {
            bool scenarioVizChanged = scenarioVizPrev.type != simScenario.type ||
                                     std::abs(scenarioVizPrev.speed_mph - simScenario.speed_mph) > 1e-4f ||
                                     std::abs(scenarioVizPrev.intensity - simScenario.intensity) > 1e-4f ||
                                     std::abs(scenarioVizPrev.duration_s - simScenario.duration_s) > 1e-4f;
            bool springRebuild = (std::abs(springParams.baseStiffness - springStiffnessPrev) > 1e-4f) ||
                                 (constraintsEnabled != constraintsEnabledPrev) ||
                                 !sameMaterial(springMaterialPrev, springParams.material);
            if (scenarioVizChanged) {
                scenarioVizPrev = simScenario;
                if (auto* node = scene.find(scene.activeModelId()); node && node->mesh && !physicsSimEnabled) {
                    refreshWeaknessVisualization(*node->mesh, meshInsight, simScenario);
                    meshDirty_ = true;
                }
            }
            if (springRebuild && physicsSimEnabled) {
                springStiffnessPrev = springParams.baseStiffness;
                constraintsEnabledPrev = constraintsEnabled;
                springMaterialPrev = springParams.material;
                if (auto* node = scene.find(scene.activeModelId()); node && node->mesh) {
                    if (meshInsight.meshRestPositions.size() == node->mesh->positions.size())
                        node->mesh->positions = meshInsight.meshRestPositions;
                    node->mesh->recomputeNormals();
                    if (!massSpring.build(*node->mesh, meshInsight.triScenarioSource, springParams,
                                          meshInsight.meshUnitToMm, constraintsEnabled))
                        commandLog_ += "[sim] Mass-spring rebuild failed.\n";
                    meshInsight.triStrainStress.clear();
                    simScenarioTime = 0.0;
                    refreshWeaknessVisualization(*node->mesh, meshInsight, simScenario);
                    meshDirty_ = true;
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(14.f, ds.y - logBarH - 14.f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(400.f, ds.x - 28.f), logBarH), ImGuiCond_FirstUseEver);
        ImGui::Begin("Log");
        ImGui::TextUnformatted(commandLog_.c_str());
        ImGui::End();

        {
            auto* pickNode = scene.find(scene.activeModelId());
            if (pickNode && pickNode->mesh) {
                pickNode->mesh->ensurePickBuffer();
                uint32_t newHover = UINT32_MAX;
                const bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                int fbW = 0, fbH = 0;
                glfwGetFramebufferSize(window, &fbW, &fbH);
                double mx = 0, my = 0;
                glfwGetCursorPos(window, &mx, &my);

                if (enableMeshInsight && fbW > 0 && fbH > 0 && !rmb && !ImGui::GetIO().WantCaptureMouse) {
                    glm::vec3 ro{}, rd{};
                    rendering::cameraViewportRay(camera, static_cast<float>(mx), static_cast<float>(my),
                                                 static_cast<float>(fbW), static_cast<float>(fbH), ro, rd);
                    float t = 0.f;
                    uint32_t tri = 0;
                    if (rendering::pickMeshTriangle(ro, rd, pickNode->transform, *pickNode->mesh, t, tri))
                        newHover = tri;
                }

                if (newHover != hoverTriIndex) {
                    hoverTriIndex = newHover;
                    meshDirty_ = true;
                }

                pickNode->mesh->clearPickHighlight();
                if (enableMeshInsight) {
                    if (hoverTriIndex != UINT32_MAX) pickNode->mesh->setTrianglePickWeight(hoverTriIndex, 0.42f);
                    if (selectedTriIndex != UINT32_MAX) pickNode->mesh->setTrianglePickWeight(selectedTriIndex, 0.78f);
                }
                static uint32_t lastPickUploadSig = 0xFFFFFFFFu;
                const uint32_t pickUploadSig =
                    enableMeshInsight ? (hoverTriIndex ^ (selectedTriIndex * 1315423911u)) : 0xA11u;
                if (pickUploadSig != lastPickUploadSig) {
                    lastPickUploadSig = pickUploadSig;
                    meshDirty_ = true;
                }

                if (!rmb && !ImGui::GetIO().WantCaptureMouse) {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        if (ImGui::GetIO().KeyShift) {
                            if (hoverTriIndex != UINT32_MAX)
                                commandLog_ += "[insight] Shift+click: regional apply not implemented (triangle " +
                                                std::to_string(hoverTriIndex) + ").\n";
                        } else if (hoverTriIndex != UINT32_MAX) {
                            selectedTriIndex = hoverTriIndex;
                            showFaceInspector = true;
                            meshDirty_ = true;
                        }
                    }
                }

                if (enableMeshInsight && hoverTriIndex != UINT32_MAX && !ImGui::GetIO().WantCaptureMouse && !rmb) {
                    const geometry::Mesh& mesh = *pickNode->mesh;
                    const uint32_t ht = hoverTriIndex;
                    const float weak = (meshInsight.valid && ht < meshInsight.triWeakness.size())
                                           ? meshInsight.triWeakness[ht]
                                           : 0.f;
                    const float stress =
                        (meshInsight.valid && ht < meshInsight.triStress.size()) ? meshInsight.triStress[ht] : 0.f;
                    float combDyn = weak;
                    float velW = 0.f, loadW = 0.f, propTri = 0.f;
                    glm::vec3 defectDir{};
                    if (meshInsight.valid && ht < meshInsight.triScenarioSource.size()) {
                        std::vector<analysis::TriangleWeakness> one = {meshInsight.triScenarioSource[ht]};
                        float sp = 0.f, ap = 0.f, cr = 0.f;
                        sim::scenarioToVisualizationSliders(simScenario, sp, ap, cr);
                        analysis::applyKinematicWeaknessProxies(one, sp, ap, cr);
                        combDyn = analysis::TriangleWeakness::combined(one[0], weaknessStressScale,
                                                                       weaknessVelocityScale, weaknessLoadScale);
                        velW = one[0].velocityWeight;
                        loadW = one[0].loadWeight;
                        defectDir = one[0].defectDirection;
                        if (ht < meshInsight.triPropagated.size()) propTri = meshInsight.triPropagated[ht];
                    }
                    const float thick = triangleMinEdgeMm(mesh, ht, meshUnitToMm_);
                    ImGui::BeginTooltip();
                    ImGui::Text("Mesh: %s", scene.activeModelId().c_str());
                    ImGui::Text("Face (triangle) ID: %u", ht);
                    ImGui::Separator();
                    ImGui::Text("Severity band (AI merge): %s", severityBandLabel(weak));
                    ImGui::Text("Weighted combo (scenario + scales): %.2f", static_cast<double>(combDyn));
                    ImGui::Text("Laplacian stress proxy: %.2f (0-1, geometry-only)", static_cast<double>(stress));
                    ImGui::Text("Velocity / load weights: %.2f / %.2f", static_cast<double>(velW),
                                static_cast<double>(loadW));
                    ImGui::Text("Strain channel (spring / mix): %.2f", static_cast<double>(propTri));
                    ImGui::Text("Min edge (thickness proxy): %.2f mm", static_cast<double>(thick));
                    if (ImGui::GetIO().KeyCtrl) {
                        size_t triBase = static_cast<size_t>(ht) * 3;
                        if (triBase + 2 < mesh.indices.size()) {
                            uint32_t ia = mesh.indices[triBase];
                            uint32_t ib = mesh.indices[triBase + 1];
                            uint32_t ic = mesh.indices[triBase + 2];
                            if (ia < mesh.positions.size() && ib < mesh.positions.size() &&
                                ic < mesh.positions.size()) {
                                glm::vec3 va = mesh.positions[ia];
                                glm::vec3 vb = mesh.positions[ib];
                                glm::vec3 vc = mesh.positions[ic];
                                glm::vec3 fn = glm::normalize(glm::cross(vb - va, vc - va));
                                ImGui::Text("Face normal: %.3f %.3f %.3f", static_cast<double>(fn.x),
                                            static_cast<double>(fn.y), static_cast<double>(fn.z));
                            }
                        }
                        ImGui::Text("Defect dir hint: %.2f %.2f %.2f", static_cast<double>(defectDir.x),
                                    static_cast<double>(defectDir.y), static_cast<double>(defectDir.z));
                        ImGui::Text("Touches open boundary: %s",
                                    triangleHasBoundaryEdge(meshInsight, mesh, ht) ? "yes" : "no");
                    }
                    ImGui::Separator();
                    ImGui::TextWrapped("%s",
                                       faceSuggestionText(meshInsight.merged, weak, stress).c_str());
                    ImGui::EndTooltip();
                }
            } else {
                if (hoverTriIndex != UINT32_MAX) {
                    hoverTriIndex = UINT32_MAX;
                    meshDirty_ = true;
                }
            }
        }

        if (showFaceInspector && selectedTriIndex != UINT32_MAX) {
            auto* pn = scene.find(scene.activeModelId());
            if (!pn || !pn->mesh) {
                showFaceInspector = false;
            } else {
                ImGui::Begin("Face inspector", &showFaceInspector);
                ImGui::Text("Mesh: %s", scene.activeModelId().c_str());
                ImGui::Text("Triangle ID: %u", selectedTriIndex);
                float wk = (meshInsight.valid && selectedTriIndex < meshInsight.triWeakness.size())
                               ? meshInsight.triWeakness[selectedTriIndex]
                               : 0.f;
                float st = (meshInsight.valid && selectedTriIndex < meshInsight.triStress.size())
                               ? meshInsight.triStress[selectedTriIndex]
                               : 0.f;
                float combD = wk;
                float vW = 0.f, lW = 0.f, pr = 0.f;
                if (meshInsight.valid && selectedTriIndex < meshInsight.triScenarioSource.size()) {
                    std::vector<analysis::TriangleWeakness> one = {meshInsight.triScenarioSource[selectedTriIndex]};
                    float sp = 0.f, ap = 0.f, cr = 0.f;
                    sim::scenarioToVisualizationSliders(simScenario, sp, ap, cr);
                    analysis::applyKinematicWeaknessProxies(one, sp, ap, cr);
                    combD = analysis::TriangleWeakness::combined(one[0], weaknessStressScale, weaknessVelocityScale,
                                                                 weaknessLoadScale);
                    vW = one[0].velocityWeight;
                    lW = one[0].loadWeight;
                    if (selectedTriIndex < meshInsight.triPropagated.size())
                        pr = meshInsight.triPropagated[selectedTriIndex];
                }
                ImGui::Text("Severity band (AI merge): %s", severityBandLabel(wk));
                ImGui::Text("Weighted combo: %.2f", static_cast<double>(combD));
                ImGui::Text("Stress proxy: %.2f", static_cast<double>(st));
                ImGui::Text("Velocity / load: %.2f / %.2f", static_cast<double>(vW), static_cast<double>(lW));
                ImGui::Text("Strain (spring): %.2f", static_cast<double>(pr));
                ImGui::Separator();
                if (ImGui::Button("Apply AI suggestion (placeholder)"))
                    commandLog_ += "[insight] Apply suggestion: not automated — use geometry tools / export.\n";
                if (ImGui::Button("Run localized FEA"))
                    commandLog_ += "[insight] Localized FEA: not implemented (full GPU solver TBD).\n";
                ImGui::InputTextMultiline("Bookmark note", faceBookmarkBuf.data(), faceBookmarkBuf.size(),
                                          ImVec2(-1, 72));
                if (ImGui::Button("Save bookmark to log"))
                    commandLog_ += "[bookmark] " + scene.activeModelId() + " tri " +
                                    std::to_string(selectedTriIndex) + ": " +
                                    std::string(faceBookmarkBuf.data()) + "\n";
                ImGui::End();
            }
        }

        {
            nlohmann::json j;
            j["active"] = scene.activeModelId();
            j["ids"] = scene.ids();
            if (ipcPort > 0)
                j["ipc_url"] = "http://" + ipcListenHost + ":" + std::to_string(ipcPort);
            std::lock_guard<std::mutex> lk(ipcApiMu);
            ipcSceneJson = j.dump();
            auto* n = scene.find(scene.activeModelId());
            if (n && n->mesh) {
                std::string se;
                n->mesh->saveStlBinaryToVector(ipcStlBlob, se);
            } else {
                ipcStlBlob.clear();
            }
        }

        if (ipcPort > 0) {
            ImGui::SetNextWindowPos(ImVec2(std::max(420.f, ds.x - 416.f), ds.y - 248.f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(396.f, 232.f), ImGuiCond_FirstUseEver);
            ImGui::Begin("API");
            ImGui::TextWrapped(
                "HTTP API — use curl, PowerShell, or your own app. "
                "Mutations are applied on the next frame (Vulkan-safe).");
            ImGui::Text("Base URL: http://%s:%u", ipcListenHost.c_str(), static_cast<unsigned>(ipcPort));
            ImGui::BulletText("GET  /v1/health");
            ImGui::BulletText("GET  /v1/scene");
            ImGui::BulletText("GET  /v1/mesh/stl  (binary STL of active mesh)");
            ImGui::BulletText("POST /v1/stl/load  body: {\"path\":\"C:/.../file.stl\"}");
            ImGui::BulletText("POST /v1/stl/export body: {\"path\":\"C:/.../out.stl\"}");
            ImGui::BulletText("POST /v1/command    body: <command JSON>");
            ImGui::BulletText("POST /v1/scene/active body: {\"id\":\"modelId\"}");
            ImGui::End();
        }

        if (meshDirty_) {
            auto* node = scene.find(scene.activeModelId());
            if (node && node->mesh) {
                geometry::Mesh& mesh = *node->mesh;
                const std::vector<glm::vec3>* restPtr =
                    (meshInsight.valid && meshInsight.meshRestPositions.size() == mesh.positions.size())
                        ? &meshInsight.meshRestPositions
                        : nullptr;
                const bool needVizDeform =
                    restPtr != nullptr && std::abs(deformExaggeration - 1.f) > 1e-4f;
                const bool needVizSmooth = vizSmoothPasses > 0 && !mesh.defectHighlight.empty();

                if (needVizDeform || needVizSmooth) {
                    std::vector<glm::vec3> posBk = mesh.positions;
                    std::vector<glm::vec3> nrmBk = mesh.normals;
                    std::vector<glm::vec4> dhBk = mesh.defectHighlight;
                    std::vector<float> wpBk = mesh.weaknessPropagated;

                    mesh.defectHighlight = dhBk;
                    mesh.weaknessPropagated = wpBk;
                    if (needVizSmooth) {
                        static std::vector<std::vector<uint32_t>> vizNeighbors;
                        rendering::meshBuildVertexNeighbors(mesh, vizNeighbors);
                        rendering::meshSmoothVertexVec4(vizNeighbors, vizSmoothPasses, vizSmoothLambda,
                                                       mesh.defectHighlight);
                        rendering::meshSmoothVertexScalars(vizNeighbors, vizSmoothPasses, vizSmoothLambda,
                                                          mesh.weaknessPropagated);
                    }
                    if (needVizDeform) {
                        const std::vector<glm::vec3>& rest = meshInsight.meshRestPositions;
                        for (size_t i = 0; i < mesh.positions.size(); ++i)
                            mesh.positions[i] = rest[i] + (posBk[i] - rest[i]) * deformExaggeration;
                        mesh.recomputeNormals();
                    }
                    vk.uploadMesh(mesh, uploadErr, restPtr);
                    mesh.positions = std::move(posBk);
                    mesh.normals = std::move(nrmBk);
                    mesh.defectHighlight = std::move(dhBk);
                    mesh.weaknessPropagated = std::move(wpBk);
                } else
                    vk.uploadMesh(mesh, uploadErr, restPtr);
            }
            meshDirty_ = false;
        }

        if (!vk.beginFrame()) continue;
        glm::mat4 model(1.f);
        auto* node = scene.find(scene.activeModelId());
        if (node) model = node->transform;
        if (node && node->mesh) {
            rendering::MeshDefectViewParams dv{};
            dv.stressScale = weaknessStressScale;
            dv.velocityScale = weaknessVelocityScale;
            dv.loadScale = weaknessLoadScale;
            dv.visualMode = weaknessVisualMode;
            dv.timeMix = weaknessTimeMix;
            dv.dynamicNormalization = vizDynamicNormalization;
            dv.strainAlert = vizStrainAlert;
            dv.strainAlertThreshold = vizStrainThreshold;
            dv.strainAlertBlink = vizStrainBlink;
            dv.directionVizWeight = vizDirectionWeight;
            dv.vizTimeSec = static_cast<float>(glfwGetTime());
            if (vizDynamicNormalization && !node->mesh->defectHighlight.empty()) {
                rendering::meshComputeMixedRange(*node->mesh, dv, weaknessTimeMix, dv.heatRangeMin,
                                                   dv.heatRangeMax);
            } else {
                dv.heatRangeMin = 0.f;
                dv.heatRangeMax = 1.f;
            }
            vk.recordMeshPass(*node->mesh, model, camera, dv);
        }
        imgui.render(vk.commandBuffer());
        vk.endFrame();
    }

    ipcServer.reset();
    gpuLaplacian.reset();

    vkDeviceWaitIdle(vk.device());
    imgui.shutdown(vk);
    vk.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace physisim::core
