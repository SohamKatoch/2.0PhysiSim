#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include "fem/FemTypes.h"
#include "fem/TetrahedralMesh.h"

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::core {

struct SceneNode {
    std::string id;
    std::shared_ptr<geometry::Mesh> mesh;
    glm::mat4 transform{1.f};
    /// Optional solid mesh for external FEM (e.g. CalculiX C3D4); independent of surface `mesh`.
    std::shared_ptr<fem::TetrahedralMesh> volumeMesh;
    std::optional<fem::FemResult> lastFemResult;
};

class Scene {
public:
    std::string& activeModelId() { return activeId_; }
    const std::string& activeModelId() const { return activeId_; }

    void clear();
    void addOrReplace(std::string id, std::shared_ptr<geometry::Mesh> mesh);
    bool remove(const std::string& id);

    SceneNode* find(const std::string& id);
    const SceneNode* find(const std::string& id) const;

    std::vector<std::string> ids() const;

private:
    std::unordered_map<std::string, SceneNode> nodes_;
    std::string activeId_;
};

} // namespace physisim::core
