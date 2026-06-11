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

    int provinceCount() const { return m_provinceCount; }
    int civCount() const { return m_civCount; }

    // Selection : a partir d'une direction unitaire en espace modele (issue d'un
    // ray-sphere), renvoie la province et la civilisation touchees.
    void pick(const glm::vec3& dir, int& province, int& civ) const;

    glm::vec3 civColor(int civ) const;
    int provinceCiv(int province) const;

private:
    void build(const PlanetMesh& planet, const Params& params);

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    int m_indexCount = 0;
    int m_provinceCount = 0;
    int m_civCount = 0;

    std::vector<glm::vec3> m_provinceSeeds;
    std::vector<int> m_provinceCiv;
    std::vector<glm::vec3> m_civColors;
};

} // namespace wl
