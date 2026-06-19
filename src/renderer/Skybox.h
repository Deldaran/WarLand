#pragma once

#include <string>

namespace wl {

// Skybox cubemap (6 faces) : fond etoile/nebuleuse rendu a l'infini derriere
// toute la scene. Suit la rotation de la camera (mais pas sa translation) ->
// le ciel profond reste fixe dans l'espace quand on tourne/se deplace.
class Skybox {
public:
    Skybox() = default;
    ~Skybox();

    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;

    // Charge les 6 faces <prefix>_RIGHT/LEFT/UP/DOWN/FRONT/BACK.png dans dir.
    bool load(const std::string& dir, const std::string& prefix);

    bool valid() const { return m_tex != 0; }

    // Lie le cubemap sur l'unite 0 et dessine le cube (36 sommets).
    void draw() const;

private:
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_tex = 0;
};

} // namespace wl
