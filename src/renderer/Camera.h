#pragma once

#include <glm/glm.hpp>

namespace wl {

// Camera a deux modes :
//  - Orbit  : tourne autour du centre de la planete (vue spatiale).
//  - Surface: vol/RTS au ras du sol -> ZQSD pour se deplacer, souris pour
//             regarder (y compris vers le haut), molette = altitude.
// On bascule automatiquement de l'un a l'autre selon le zoom.
class Camera {
public:
    enum class Mode { Orbit, Surface };

    Camera(float aspect);

    void setAspect(float aspect) { m_aspect = aspect; }

    Mode mode() const { return m_mode; }
    void enterSurface();
    void enterOrbit();

    // --- Mode orbite ---
    void orbit(float deltaYawDeg, float deltaPitchDeg);
    void zoom(float deltaScroll); // molette (altitude dans les deux modes)
    void setDistance(float d);
    float distance() const { return m_distance; }

    // --- Mode surface (vol / RTS) ---
    void surfaceLook(float dYawDeg, float dPitchDeg);          // souris : regarder
    void surfaceMove(float forward, float strafe, float dist); // ZQSD : se deplacer
    glm::vec3 surfaceDir() const { return m_surfDir; }         // position sur le globe

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix() const; // near/far adaptatifs (precision depth)
    glm::vec3 position() const;

    const glm::vec3& target() const { return m_target; }

    // 0 en orbite, 1 au sol (utilise pour le LOD / suivi de terrain).
    float surfaceFactor() const { return m_mode == Mode::Surface ? 1.0f : 0.0f; }

private:
    glm::vec3 tangentFrame(glm::vec3& east, glm::vec3& north) const; // repere local

    Mode m_mode = Mode::Orbit;

    glm::vec3 m_target{0.0f};
    float m_distance = 3.0f;   // rayon (orbite ou altitude depuis le centre)
    float m_yaw = 45.0f;       // orbite : angle horizontal
    float m_pitch = 20.0f;     // orbite : angle vertical
    float m_aspect;
    float m_fovDeg = 55.0f;

    // Mode surface
    glm::vec3 m_surfDir{0.0f, 0.0f, 1.0f}; // position sur le globe (unitaire)
    float m_lookYaw = 0.0f;                // cap (degres)
    float m_lookPitch = -8.0f;             // elevation du regard (degres)

    float m_minDistance = 1.0008f;
    float m_maxDistance = 300.0f; // assez pour voir tout le systeme depuis une planete
};

} // namespace wl
