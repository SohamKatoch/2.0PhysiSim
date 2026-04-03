#include "client/ClientApp.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>

#include <GLFW/glfw3.h>

#include <httplib.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include "geometry/Mesh.h"
#include "rendering/Camera.h"
#include "rendering/VulkanRenderer.h"
#include "ui/ImGuiLayer.h"

namespace physisim::client {

namespace {

static int parsePort(int argc, char** argv, int defaultPort) {
    try {
        for (int i = 1; i < argc; ++i) {
            std::string_view ar = argv[i];
            if (ar == "--port" && i + 1 < argc) return std::stoi(argv[++i]);
            if (ar.size() > 8 && ar.substr(0, 8) == "--port=") return std::stoi(std::string(ar.substr(8)));
        }
    } catch (...) {
    }
    return defaultPort;
}

static std::string parseHost(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--host" && i + 1 < argc) return std::string(argv[++i]);
        if (a.size() > 8 && a.substr(0, 8) == "--host=") return std::string(a.substr(8));
    }
    return "127.0.0.1";
}

static void printHelp() {
    std::printf(
        "physisim_client — Vulkan remote viewer for PhysiSim IPC\n"
        "  Start server: physisim --ipc-port 17500\n"
        "  Then: physisim_client [--host 127.0.0.1] [--port 17500]\n"
        "  --help  this message\n");
}

} // namespace

int ClientApp::run(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--help") {
            printHelp();
            return 0;
        }
    }

    const std::string host = parseHost(argc, argv);
    const int serverPort = parsePort(argc, argv, 17500);

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    int w = 1200, h = 800;
    GLFWwindow* window =
        glfwCreateWindow(w, h, "PhysiSim Vulkan Client (remote IPC)", nullptr, nullptr);
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

    struct WindowUserData {
        ClientApp* app{nullptr};
        rendering::VulkanRenderer* vk{nullptr};
    } ud{this, &vk};
    glfwSetWindowUserPointer(window, &ud);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int ww, int hh) {
        auto* u = static_cast<WindowUserData*>(glfwGetWindowUserPointer(win));
        if (u && u->vk) u->vk->resize(ww, hh);
    });
    glfwSetScrollCallback(window, [](GLFWwindow* win, double, double yoff) {
        auto* u = static_cast<WindowUserData*>(glfwGetWindowUserPointer(win));
        if (u && u->app) u->app->addScrollDelta(static_cast<float>(yoff));
    });

    rendering::Camera camera;
    camera.setAspect(static_cast<float>(w) / static_cast<float>(h));

    geometry::Mesh displayMesh;
    bool hasMesh = false;
    std::string uploadErr;

    std::vector<char> hostBuf(256, 0);
    std::vector<char> portBuf(32, 0);
    std::strncpy(hostBuf.data(), host.c_str(), hostBuf.size() - 1);
    std::snprintf(portBuf.data(), portBuf.size(), "%d", serverPort);

    std::string statusLine = "Connect to physisim with --ipc-port, then use buttons below.";
    std::string sceneDump;
    std::string lastHttpError;

    double lx = 0, ly = 0;
    glfwGetCursorPos(window, &lx, &ly);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

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

        ImGui::Begin("Remote server");
        ImGui::InputText("Host", hostBuf.data(), hostBuf.size());
        ImGui::InputText("Port", portBuf.data(), portBuf.size());
        int p = std::max(1, std::min(65535, std::atoi(portBuf.data())));
        const std::string h(hostBuf.data());

        if (ImGui::Button("GET /v1/health")) {
            httplib::Client cli(h.c_str(), p);
            cli.set_connection_timeout(2, 0);
            auto res = cli.Get("/v1/health");
            if (res && res->status == 200)
                statusLine = "health: " + res->body;
            else
                lastHttpError = res ? ("HTTP " + std::to_string(res->status)) : "connection failed";
        }
        ImGui::SameLine();
        if (ImGui::Button("GET /v1/scene")) {
            httplib::Client cli(h.c_str(), p);
            cli.set_connection_timeout(2, 0);
            auto res = cli.Get("/v1/scene");
            if (res && res->status == 200) {
                sceneDump = res->body;
                try {
                    auto j = nlohmann::json::parse(sceneDump);
                    statusLine = "scene ok; active=" + j.value("active", std::string());
                } catch (...) {
                    statusLine = "scene (raw)";
                }
            } else {
                lastHttpError = res ? ("HTTP " + std::to_string(res->status)) : "connection failed";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Pull mesh (Vulkan)")) {
            httplib::Client cli(h.c_str(), p);
            cli.set_read_timeout(120, 0);
            auto res = cli.Get("/v1/mesh/stl");
            if (res && res->status == 200 && !res->body.empty()) {
                std::string pe;
                displayMesh.clear();
                if (!geometry::Mesh::loadStlMemory(res->body.data(), res->body.size(), displayMesh, pe)) {
                    statusLine = "STL parse: " + pe;
                    hasMesh = false;
                } else {
                    vk.uploadMesh(displayMesh, uploadErr);
                    hasMesh = true;
                    statusLine = "Loaded mesh: " + std::to_string(displayMesh.indices.size() / 3) + " tris";
                    if (!uploadErr.empty()) statusLine += " (gpu: " + uploadErr + ")";
                }
            } else {
                lastHttpError =
                    res ? ("GET /v1/mesh/stl HTTP " + std::to_string(res->status) + " " + res->body)
                        : "connection failed";
                hasMesh = false;
            }
        }
        ImGui::TextWrapped("%s", statusLine.c_str());
        if (!lastHttpError.empty()) {
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f), "%s", lastHttpError.c_str());
            if (ImGui::Button("Clear error")) lastHttpError.clear();
        }
        ImGui::End();

        ImGui::Begin("Scene JSON");
        ImGui::TextUnformatted(sceneDump.empty() ? "(fetch scene)" : sceneDump.c_str());
        ImGui::End();

        ImGui::Begin("Viewport (Vulkan)");
        ImGui::TextWrapped("Right-drag orbit, wheel zoom, Esc quit. Mesh from server renders in the main "
                           "window behind ImGui.");
        ImGui::End();

        if (!vk.beginFrame()) continue;
        glm::mat4 model(1.f);
        if (hasMesh) vk.recordMeshPass(displayMesh, model, camera);
        imgui.render(vk.commandBuffer());
        vk.endFrame();
    }

    vkDeviceWaitIdle(vk.device());
    imgui.shutdown(vk);
    vk.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace physisim::client
