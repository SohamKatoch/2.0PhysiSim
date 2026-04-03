#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace physisim::rendering {

class Camera {
public:
    void orbit(float yawDelta, float pitchDelta, float distanceDelta);
    void setAspect(float a) { aspect_ = a; }

    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix() const;

    /// World-space camera position (matches `viewMatrix` look-at eye).
    glm::vec3 eyePosition() const;

    glm::vec3 target() const { return target_; }

private:
    glm::vec3 target_{0.f, 0.f, 0.f};
    float yaw_{0.6f};
    float pitch_{0.35f};
    float distance_{3.5f};
    float aspect_{16.f / 9.f};
    float fov_{1.0f};
};

} // namespace physisim::rendering
