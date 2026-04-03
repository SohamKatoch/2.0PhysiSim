#include "core/Scene.h"

#include "geometry/Mesh.h"

namespace physisim::core {

void Scene::clear() {
    nodes_.clear();
    activeId_.clear();
}

void Scene::addOrReplace(std::string id, std::shared_ptr<geometry::Mesh> mesh) {
    SceneNode n;
    n.id = id;
    n.mesh = std::move(mesh);
    n.transform = glm::mat4(1.f);
    nodes_[id] = std::move(n);
    if (activeId_.empty()) activeId_ = id;
}

bool Scene::remove(const std::string& id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    nodes_.erase(it);
    if (activeId_ == id) {
        activeId_.clear();
        if (!nodes_.empty()) activeId_ = nodes_.begin()->first;
    }
    return true;
}

SceneNode* Scene::find(const std::string& id) {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

const SceneNode* Scene::find(const std::string& id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

std::vector<std::string> Scene::ids() const {
    std::vector<std::string> out;
    out.reserve(nodes_.size());
    for (const auto& [k, _] : nodes_) out.push_back(k);
    return out;
}

} // namespace physisim::core
