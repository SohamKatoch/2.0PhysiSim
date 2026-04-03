#include "rendering/Camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace physisim::rendering {

void Camera::orbit(float yawDelta, float pitchDelta, float distanceDelta) {
    yaw_ += yawDelta;
    pitch_ += pitchDelta;
    pitch_ = std::clamp(pitch_, -1.4f, 1.4f);
    distance_ = std::max(0.2f, distance_ + distanceDelta);
}

glm::vec3 Camera::eyePosition() const {
    float cp = std::cos(pitch_);
    glm::vec3 eye;
    eye.x = target_.x + distance_ * std::cos(yaw_) * cp;
    eye.y = target_.y + distance_ * std::sin(pitch_);
    eye.z = target_.z + distance_ * std::sin(yaw_) * cp;
    return eye;
}

glm::mat4 Camera::viewMatrix() const {
    float cp = std::cos(pitch_);
    glm::vec3 eye;
    eye.x = target_.x + distance_ * std::cos(yaw_) * cp;
    eye.y = target_.y + distance_ * std::sin(pitch_);
    eye.z = target_.z + distance_ * std::sin(yaw_) * cp;
    return glm::lookAt(eye, target_, glm::vec3(0.f, 1.f, 0.f));
}

glm::mat4 Camera::projMatrix() const {
    return glm::perspective(fov_, aspect_, 0.1f, 200.f);
}

} // namespace physisim::rendering
