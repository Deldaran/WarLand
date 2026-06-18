#pragma once

#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace wl {

class PlanetMesh;

// Couche de "props" nature en billboards 2D (style Elder Scrolls : Daggerfall) :
// arbres, sapins, buissons, cactus, rochers, touffes d'herbe. Chaque prop est un
// sprite plaque sur un quad qui fait toujours face a la camera tout en restant
// vertical (debout sur le sol). Les props sont disperses sur les terres du mesh
// selon le relief et la latitude (biome), et ne s'affichent qu'a proximite.
//
// L'atlas de sprites est procedural par defaut (placeholders) ; si des PNG sont
// presents dans assets/textures/props/ (arbre.png, sapin.png, ...), ils sont
// charges et remplacent les placeholders correspondants.
class PropLayer {
public:
    // Index des tuiles dans l'atlas (cols = 3).
    enum Type : int { Arbre = 0, Sapin = 1, Buisson = 2, Cactus = 3, Rocher = 4, Herbe = 5 };

    PropLayer() = default;
    ~PropLayer();

    PropLayer(const PropLayer&) = delete;
    PropLayer& operator=(const PropLayer&) = delete;

    // Construit la liste d'instances (positions sur le relief, taille, type) +
    // les buffers GPU, et prepare l'atlas de sprites.
    void build(const PlanetMesh& mesh, uint32_t seed, const std::string& assetsDir);

    int instanceCount() const { return m_count; }
    int atlasCols() const { return kCols; }

    // Rendu instancie. L'appelant aura regle l'etat : depth test ON, depth mask
    // ON, blend OFF, cull OFF, et lie le shader + uniformes. La texture d'atlas
    // est liee sur l'unite 0.
    void draw() const;

private:
    static constexpr int kCols = 3; // atlas 3x3 tuiles

    void buildAtlas(const std::string& assetsDir);

    unsigned int m_vao = 0;
    unsigned int m_quadVbo = 0;
    unsigned int m_instVbo = 0;
    unsigned int m_tex = 0;
    int m_count = 0;
};

} // namespace wl
