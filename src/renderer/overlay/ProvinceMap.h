#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace wl {

class PlanetMesh;

// Decoupe la surface de la planete en provinces (Voronoi spherique) regroupees
// en civilisations. Construit un overlay GPU colore par civilisation, pose
// juste au-dessus du terrain et active par le filtre "Politique".
//
// C'est la premiere couche data-driven : a terme chaque province portera ses
// ressources, sa population, son appartenance politique (cf. GamePlan).
class ProvinceMap {
public:
    struct Params {
        int provinces = 200;   // nombre de provinces sur le globe
        int civs = 7;          // nombre de civilisations
        uint32_t seed = 2024u;
    };

    ProvinceMap(const PlanetMesh& planet, const Params& params);
    ~ProvinceMap();

    ProvinceMap(const ProvinceMap&) = delete;
    ProvinceMap& operator=(const ProvinceMap&) = delete;

    void draw() const;
    void drawBorders() const; // lignes de frontiere entre civilisations

    // Reconstruit les frontieres a partir du proprietaire courant de chaque
    // province (owner indexe par id ; -2 = ocean, -1 = sauvage). Une frontiere
    // est tracee entre deux proprietaires differents, hors ocean.
    void rebuildBorders(const std::vector<int>& owner);

    int provinceCount() const { return m_provinceCount; }
    int civCount() const { return m_civCount; }

    // Selection : a partir d'une direction unitaire en espace modele (issue d'un
    // ray-sphere), renvoie la province et la civilisation touchees.
    void pick(const glm::vec3& dir, int& province, int& civ) const;

    glm::vec3 civColor(int civ) const;
    int provinceCiv(int province) const;

    // Donnees geographiques par province (pour initialiser la simulation).
    float provinceElevation(int province) const; // [-1,1] moyenne
    float provinceLatitude(int province) const;   // 0 equateur -> 1 pole

    // Graphe d'adjacence : provinces partageant une frontiere (commerce/migration).
    const std::vector<int>& neighbors(int province) const;

    // Recolore l'overlay a partir d'une couleur par province (heatmap, etc.).
    // Re-uploade le VBO ; a appeler quand les donnees changent, pas chaque frame.
    void setProvinceColors(const std::vector<glm::vec3>& provinceColors);

    // Restaure la coloration politique (couleur de civilisation).
    void applyPoliticalColors();

private:
    void build(const PlanetMesh& planet, const Params& params);
    void uploadInterleaved(const std::vector<glm::vec3>& vertexColors);

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    int m_indexCount = 0;

    unsigned int m_borderVao = 0;
    unsigned int m_borderVbo = 0;
    int m_borderVertexCount = 0;
    int m_provinceCount = 0;
    int m_civCount = 0;

    std::vector<glm::vec3> m_provinceSeeds;
    std::vector<int> m_provinceCiv;
    std::vector<glm::vec3> m_civColors;
    std::vector<float> m_provinceElevation;
    std::vector<float> m_provinceLatitude;
    std::vector<std::vector<int>> m_neighbors;

    // Conserves pour la recoloration / reconstruction dynamiques.
    std::vector<int> m_vertexProvince;
    std::vector<glm::vec3> m_overlayPositions; // positions deja surelevees
    std::vector<unsigned int> m_triIndices;    // connectivite (pour les frontieres)
};

} // namespace wl
