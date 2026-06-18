#include "renderer/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace wl {

Camera::Camera(float aspect) : m_aspect(aspect) {}

void Camera::setDistance(float d) {
    m_distance = std::clamp(d, m_minDistance, m_maxDistance);
}

// Repere tangent local au point courant (mode surface). Renvoie la normale.
glm::vec3 Camera::tangentFrame(glm::vec3& east, glm::vec3& north) const {
    glm::vec3 n = glm::normalize(m_surfDir);
    glm::vec3 ref = std::abs(n.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                          : glm::vec3(0.0f, 1.0f, 0.0f);
    east = glm::normalize(glm::cross(ref, n));
    north = glm::cross(n, east);
    return n;
}

void Camera::enterSurface() {
    if (m_mode == Mode::Surface) return;
    m_surfDir = glm::normalize(position()); // position orbitale courante (avant bascule)
    m_mode = Mode::Surface;
    m_lookYaw = m_yaw;       // garde le cap courant
    m_lookPitch = -8.0f;     // regard legerement vers le bas (horizon)
}

void Camera::enterOrbit() {
    if (m_mode == Mode::Orbit) return;
    m_mode = Mode::Orbit;
    glm::vec3 d = glm::normalize(m_surfDir);
    m_yaw = glm::degrees(std::atan2(d.z, d.x));
    m_pitch = glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
}

void Camera::orbit(float deltaYawDeg, float deltaPitchDeg) {
    m_yaw += deltaYawDeg;
    m_pitch = std::clamp(m_pitch + deltaPitchDeg, -89.0f, 89.0f);
}

void Camera::zoom(float deltaScroll) {
    m_distance *= std::pow(0.9f, deltaScroll);
    m_distance = std::clamp(m_distance, m_minDistance, m_maxDistance);
}

void Camera::surfaceLook(float dYawDeg, float dPitchDeg) {
    m_lookYaw += dYawDeg;
    m_lookPitch = std::clamp(m_lookPitch + dPitchDeg, -89.0f, 85.0f);
}

void Camera::surfaceMove(float forward, float strafe, float dist) {
    glm::vec3 east, north;
    glm::vec3 n = tangentFrame(east, north);
    float yawR = glm::radians(m_lookYaw);
    glm::vec3 heading = east * std::cos(yawR) + north * std::sin(yawR); // direction du regard (tangent)
    glm::vec3 right = glm::normalize(glm::cross(heading, n));
    glm::vec3 step = heading * forward + right * strafe;
    if (glm::length(step) > 1e-6f) {
        // Deplacement sur la sphere : on incline la direction puis on renormalise.
        m_surfDir = glm::normalize(m_surfDir + step * dist);
    }
}

glm::vec3 Camera::position() const {
    if (m_mode == Mode::Surface) {
        return glm::normalize(m_surfDir) * m_distance;
    }
    float yaw = glm::radians(m_yaw);
    float pitch = glm::radians(m_pitch);
    glm::vec3 dir{std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                  std::cos(pitch) * std::sin(yaw)};
    return m_target + dir * m_distance;
}

glm::mat4 Camera::viewMatrix() const {
    glm::vec3 pos = position();
    if (m_mode == Mode::Orbit) {
        return glm::lookAt(pos, m_target, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    // Mode surface : vue premiere personne, regard libre (cap + elevation).
    glm::vec3 east, north;
    glm::vec3 n = tangentFrame(east, north);
    float yawR = glm::radians(m_lookYaw);
    float pitchR = glm::radians(m_lookPitch);
    glm::vec3 heading = east * std::cos(yawR) + north * std::sin(yawR);
    glm::vec3 forward = glm::normalize(heading * std::cos(pitchR) + n * std::sin(pitchR));
    return glm::lookAt(pos, pos + forward, n);
}

glm::mat4 Camera::projectionMatrix() const {
    // near/far adaptatifs selon l'altitude -> bonne precision du depth buffer
    // (evite de voir a travers la planete).
    float alt = std::max(0.0f, m_distance - 1.0f); // altitude au-dessus du niveau de la mer
    float nearP = std::clamp(alt * 0.25f, 0.0008f, 2.0f);
    float farP = std::max(m_distance + 8.0f, 6.0f);
    return glm::perspective(glm::radians(m_fovDeg), m_aspect, nearP, farP);
}

} // namespace wl
