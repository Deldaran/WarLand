#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace wl {

class PlanetMesh;

// Couche de "props" nature en billboards 2D (style Elder Scrolls : Daggerfall) :
// des sprites plaques sur un quad qui fait toujours face a la camera tout en
// restant vertical (debout sur le sol). Les sprites proviennent du pack de
// l'utilisateur (assets/ressources/) et sont charges dans une texture array ;
// chaque instance reference une couche (layer). Les props sont disperses sur les
// terres selon le biome (latitude + relief) : feuillus lush a l'equateur,
// conifere sombre en zone boreale, palmiers en tropical, arbres morts + rochers
// en desert/toundra, etc. Ils ne s'affichent qu'a proximite du sol.
class PropLayer {
public:
    PropLayer() = default;
    ~PropLayer();

    PropLayer(const PropLayer&) = delete;
    PropLayer& operator=(const PropLayer&) = delete;

    // Construit la texture array (sprites du pack) + les instances dispersees.
    void build(const PlanetMesh& mesh, uint32_t seed, const std::string& assetsDir);

    int instanceCount() const { return m_count; }

    // Rendu instancie. L'appelant regle l'etat (depth ON, blend OFF, cull OFF)
    // et lie le shader + uniformes. La texture array est liee sur l'unite 0.
    void draw() const;

private:
    void buildTextureArray(const std::string& assetsDir);

    unsigned int m_vao = 0;
    unsigned int m_quadVbo = 0;
    unsigned int m_instVbo = 0;
    unsigned int m_tex = 0; // GL_TEXTURE_2D_ARRAY
    int m_count = 0;
};

} // namespace wl
