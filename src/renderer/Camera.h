#pragma once

#include <glm/glm.hpp>

namespace wl {

// Camera orbitale : tourne autour d'une cible (le centre de la planete).
// Controle: drag souris = rotation, molette = zoom. C'est la base de la
// navigation orbitale -> sol decrite dans le GamePlan.
class Camera {
public:
    Camera(float aspect);

    void setAspect(float aspect) { m_aspect = aspect; }

    // Entrees normalisees (deltas souris en pixels, delta molette en crans).
    void orbit(float deltaYawDeg, float deltaPitchDeg);
    void zoom(float deltaScroll);
    void setDistance(float d) { m_distance = glm::clamp(d, m_minDistance, m_maxDistance); }
    float distance() const { return m_distance; }

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix() const;
    glm::vec3 position() const;

    const glm::vec3& target() const { return m_target; }

private:
    glm::vec3 m_target{0.0f};
    float m_distance = 3.0f;   // rayon orbital (planete de rayon ~1)
    float m_yaw = 45.0f;       // angle horizontal (degres)
    float m_pitch = 20.0f;     // angle vertical (degres)
    float m_aspect;
    float m_fovDeg = 50.0f;
    float m_near = 0.01f;
    float m_far = 500.0f;

    float m_minDistance = 1.04f; // juste au-dessus de la surface
    float m_maxDistance = 40.0f;
};

} // namespace wl
