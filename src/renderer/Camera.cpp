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
    glm::vec3 pos = position();

    // Mélange orbite -> surface selon le zoom : quand on s'approche du sol,
    // la camera cesse de regarder le centre de la planete et "redresse" son
    // angle vers l'HORIZON et le CIEL, comme si on se posait dessus.
    float landT = 1.0f - std::clamp((m_distance - 1.07f) / (1.7f - 1.07f), 0.0f, 1.0f);
    landT = landT * landT * (3.0f - 2.0f * landT); // smoothstep

    if (landT <= 0.0001f) {
        return glm::lookAt(pos, m_target, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::vec3 n = glm::normalize(pos); // verticale locale (vers le ciel)

    // Repere tangent local (garde aux poles).
    glm::vec3 ref = std::abs(n.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                          : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 east = glm::normalize(glm::cross(ref, n));
    glm::vec3 north = glm::cross(n, east);

    // On vise un point du SOL situe devant soi (dans la direction du cap) : le
    // terrain occupe le bas du cadre, l'horizon et le ciel le haut.
    float yawR = glm::radians(m_yaw);
    glm::vec3 heading = east * std::cos(yawR) + north * std::sin(yawR);
    glm::vec3 groundAhead = glm::normalize(n + heading * 0.55f); // ~29 deg en avant

    glm::vec3 orbitTarget = m_target;
    glm::vec3 surfTarget = groundAhead; // point a la surface (rayon ~1)
    glm::vec3 target = glm::mix(orbitTarget, surfTarget, landT);
    glm::vec3 up = glm::normalize(glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), n, landT));

    return glm::lookAt(pos, target, up);
}

glm::mat4 Camera::projectionMatrix() const {
    return glm::perspective(glm::radians(m_fovDeg), m_aspect, m_near, m_far);
}

} // namespace wl
