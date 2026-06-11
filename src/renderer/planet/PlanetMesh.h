#pragma once

#include <cstdint>

namespace wl {

// Genere une planete : icosphere subdivisee + relief procedural (FBM Perlin).
// Le mesh est indexe (positions partagees) et stocke pour chaque sommet sa
// position, sa normale et son elevation normalisee (utilisee par le shader
// pour colorer les biomes).
class PlanetMesh {
public:
    struct Params {
        int subdivisions = 6;      // niveaux de subdivision de l'icosaedre
        float seaLevel = 0.0f;     // elevation [-1,1] consideree comme niveau de la mer
        float amplitude = 0.035f;  // hauteur max du relief (en fraction du rayon)
        float baseFrequency = 1.8f;
        int octaves = 6;
        uint32_t seed = 1337u;
    };

    explicit PlanetMesh(const Params& params);
    ~PlanetMesh();

    PlanetMesh(const PlanetMesh&) = delete;
    PlanetMesh& operator=(const PlanetMesh&) = delete;

    void draw() const;

    int triangleCount() const { return m_indexCount / 3; }
    int vertexCount() const { return m_vertexCount; }

private:
    void generate(const Params& params);

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    int m_indexCount = 0;
    int m_vertexCount = 0;
};

} // namespace wl
