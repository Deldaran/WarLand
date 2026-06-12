#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace wl {

class ProvinceMap;

// Simulation du cycle de l'eau sur une grille lat/lon couvrant la planete.
// Evaporation (oceans) -> transport par les vents zonaux -> condensation /
// precipitation (avec effet orographique et ceintures subtropicales seches).
// Produit un champ de pluie (pour le sol) et un champ nuageux (pour le rendu),
// faisant emerger de vrais deserts et regions humides persistants.
class Climate {
public:
    void init(const ProvinceMap& provinces, float seaLevel, int width = 128, int height = 64);
    void step(double days, double season, double timeDays);

    // Pluie courante (0 sec .. 1 tres humide) a une direction donnee.
    float rainfallAt(const glm::vec3& dir) const;

    int width() const { return m_w; }
    int height() const { return m_h; }
    const std::vector<float>& cloudField() const { return m_cloud; }

private:
    int index(int i, int j) const { return j * m_w + i; }
    float sampleBilinear(const std::vector<float>& f, float fi, float fj) const;

    int m_w = 0;
    int m_h = 0;

    // Donnees statiques par cellule.
    std::vector<uint8_t> m_ocean;
    std::vector<float> m_elev;  // elevation moyenne (~ -1..1)
    std::vector<float> m_latN;  // latitude normalisee (-1 pole sud .. 1 pole nord)

    // Etat dynamique.
    std::vector<float> m_hum;   // humidite (vapeur d'eau)
    std::vector<float> m_rain;  // precipitation lissee (0..1)
    std::vector<float> m_cloud; // couverture nuageuse (0..1)
    std::vector<float> m_tmp;   // tampon pour l'advection
};

} // namespace wl
