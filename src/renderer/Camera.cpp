#include "renderer/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace wl {

Camera::Camera(float aspect) : m_aspect(aspect) {}

void Camera::orbit(float deltaYawDeg, float deltaPitchDeg) {
    m_yaw += deltaYawDeg;
    m_pitch += deltaPitchDeg;
    // On bloque le pitch pour eviter le retournement aux poles.
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
}

void Camera::zoom(float deltaScroll) {
    // Zoom exponentiel : naturel pour passer de l'orbite au sol.
    m_distance *= std::pow(0.9f, deltaScroll);
    m_distance = std::clamp(m_distance, m_minDistance, m_maxDistance);
}

glm::vec3 Camera::position() const {
    float yaw = glm::radians(m_yaw);
    float pitch = glm::radians(m_pitch);
    glm::vec3 dir{
        std::cos(pitch) * std::cos(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::sin(yaw)};
    return m_target + dir * m_distance;
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position(), m_target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projectionMatrix() const {
    return glm::perspective(glm::radians(m_fovDeg), m_aspect, m_near, m_far);
}

} // namespace wl
