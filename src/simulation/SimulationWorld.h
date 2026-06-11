#pragma once

#include "simulation/Components.h"

#include <entt/entt.hpp>
#include <vector>
#include <string>
#include <deque>
#include <random>

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
        bool afflicted = false;
        EventType affliction = EventType::Drought;
    };

    // Entree du journal historique (timeline).
    struct EventRecord {
        int year = 0;
        std::string text;
        int severity = 1; // 0 = positif, 1 = avertissement, 2 = grave
    };

    // Cree les entites a partir du decoupage geographique des provinces.
    void init(const ProvinceMap& provinces, float seaLevel = 0.0f);

    // Avance la simulation de `days` jours in-game (0 si en pause).
    void tick(double days, int year);

    ProvinceState state(int provinceId) const;
    double population(int provinceId) const;

    double totalPopulation() const { return m_totalPopulation; }
    double maxProvincePopulation() const { return m_maxProvincePopulation; }
    double stability() const { return m_stability; } // 0..1 (fraction sans famine)

    const std::deque<EventRecord>& events() const { return m_events; }

    static const char* biomeName(Biome b);
    static const char* eventName(EventType t);

private:
    entt::registry m_registry;
    std::vector<entt::entity> m_byProvince;  // index = id de province
    std::vector<entt::entity> m_inhabited;   // provinces non oceaniques
    std::vector<std::vector<int>> m_neighbors; // graphe d'adjacence (par id)

    double m_totalPopulation = 0.0;
    double m_maxProvincePopulation = 1.0;
    double m_stability = 1.0;

    std::mt19937 m_rng{12345u};
    std::deque<EventRecord> m_events;

    void recomputeAggregates();
    void exchangeBetweenProvinces(double days); // commerce + migration
    void spawnEvents(double days, int year);
    void logEvent(int year, std::string text, int severity);
};

} // namespace wl
