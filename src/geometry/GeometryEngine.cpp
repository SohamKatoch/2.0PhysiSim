#include "geometry/GeometryEngine.h"

#include <sstream>

#include <glm/gtc/matrix_transform.hpp>

#include "fem/TetrahedralMesh.h"
#include "geometry/Mesh.h"
#include "geometry/MeshOperations.h"

namespace physisim::geometry {

GeometryEngine::GeometryEngine(core::Scene& scene) : scene_(scene) {}

bool GeometryEngine::apply(const core::Command& cmd, std::string& errOut) {
    using core::CommandAction;
    switch (cmd.action) {
        case CommandAction::Create: {
            std::string kind = "cube";
            if (cmd.parameters.contains("primitive") && cmd.parameters["primitive"].is_string())
                kind = cmd.parameters["primitive"].get<std::string>();
            std::string id = "model";
            if (cmd.parameters.contains("id") && cmd.parameters["id"].is_string())
                id = cmd.parameters["id"].get<std::string>();
            if (kind == "cube") {
                auto mesh = std::make_shared<Mesh>(Mesh::createUnitCube());
                scene_.addOrReplace(id, mesh);
                scene_.activeModelId() = id;
                bool attachFem = cmd.parameters.contains("attach_fem_demo_volume") &&
                                 cmd.parameters["attach_fem_demo_volume"].is_boolean() &&
                                 cmd.parameters["attach_fem_demo_volume"].get<bool>();
                if (attachFem) {
                    if (core::SceneNode* n = scene_.find(id))
                        n->volumeMesh =
                            std::make_shared<fem::TetrahedralMesh>(fem::TetrahedralMesh::singleCornerTetFromUnitCube());
                }
                return true;
            }
            errOut = "Unknown primitive: " + kind;
            return false;
        }
        case CommandAction::Transform: {
            std::string id = scene_.activeModelId();
            if (cmd.target) id = *cmd.target;
            auto* node = scene_.find(id);
            if (!node || !node->mesh) {
                errOut = "Transform target not found";
                return false;
            }
            if (cmd.parameters.contains("translate")) {
                auto& tj = cmd.parameters["translate"];
                if (tj.is_array() && tj.size() >= 3) {
                    glm::vec3 t(tj[0].get<float>(), tj[1].get<float>(), tj[2].get<float>());
                    MeshOperations::translate(*node->mesh, t);
                }
            }
            if (cmd.parameters.contains("scale")) {
                float s = 1.f;
                if (cmd.parameters["scale"].is_number()) s = cmd.parameters["scale"].get<float>();
                MeshOperations::scaleUniform(*node->mesh, s);
            }
            return true;
        }
        case CommandAction::Modify:
        case CommandAction::Boolean:
        case CommandAction::Analyze:
            errOut = "Operation not implemented in geometry engine (use analysis pipeline)";
            return false;
        case CommandAction::AnalyzeFem:
            errOut = "analyze_fem is handled by the FEM adapter (not geometry engine)";
            return false;
        default:
            errOut = "Unknown action";
            return false;
    }
}

} // namespace physisim::geometry
