#pragma once

#include "simulation/Components.h"

#include <entt/entt.hpp>
#include <vector>
#include <string>

namespace wl {

class ProvinceMap;

// Monde simule : une entite EnTT par province, systemes de production /
// consommation / dynamique de population pilotes par un temps a ticks.
//
// Decouple du rendu : ne touche jamais a OpenGL. Le rendu lit l'etat via les
// accesseurs (ex. heatmap de population, panneau Contexte).
class SimulationWorld {
public:
    // Etat lisible d'une province (pour l'UI).
    struct ProvinceState {
        bool valid = false;
        bool ocean = true;
        int civ = -1;
        Biome biome = Biome::Ocean;
        double population = 0.0;
        double food = 0.0;
        double materials = 0.0;
        double energy = 0.0;
        double foodBalance = 0.0;
    };

    // Cree les entites a partir du decoupage geographique des provinces.
    void init(const ProvinceMap& provinces, float seaLevel = 0.0f);

    // Avance la simulation de `days` jours in-game (0 si en pause).
    void tick(double days);

    ProvinceState state(int provinceId) const;
    double population(int provinceId) const;

    double totalPopulation() const { return m_totalPopulation; }
    double maxProvincePopulation() const { return m_maxProvincePopulation; }
    double stability() const { return m_stability; } // 0..1 (fraction sans famine)

    static const char* biomeName(Biome b);

private:
    entt::registry m_registry;
    std::vector<entt::entity> m_byProvince; // index = id de province

    double m_totalPopulation = 0.0;
    double m_maxProvincePopulation = 1.0;
    double m_stability = 1.0;

    void recomputeAggregates();
};

} // namespace wl
