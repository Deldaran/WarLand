#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

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

    // Geometrie CPU conservee pour les couches qui se posent sur la surface
    // (overlay des provinces, frontieres, icones...).
    const std::vector<glm::vec3>& directions() const { return m_dirs; }   // unitaires
    const std::vector<glm::vec3>& positions() const { return m_positions; } // avec relief
    const std::vector<float>& elevations() const { return m_elevations; }   // [-1,1] par sommet
    const std::vector<uint32_t>& indices() const { return m_indices; }

    // Rayon du terrain (sommet le plus proche) sous une direction unitaire.
    float heightAt(const glm::vec3& dir) const;

private:
    void generate(const Params& params);

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    int m_indexCount = 0;
    int m_vertexCount = 0;

    std::vector<glm::vec3> m_dirs;
    std::vector<glm::vec3> m_positions;
    std::vector<float> m_elevations;
    std::vector<uint32_t> m_indices;
};

} // namespace wl
