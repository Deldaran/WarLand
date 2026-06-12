#pragma once

#include "simulation/Components.h"
#include "simulation/Climate.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
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
        double control = 0.0;  // emprise du proprietaire (0..100)
        double rainfall = 0.5; // meteo locale (0 sec .. 1 orage)
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
    // timeDays = temps absolu in-game (pour la meteo et les saisons).
    void tick(double days, int year, double timeDays);

    ProvinceState state(int provinceId) const;
    std::vector<ProvinceState> allStates() const; // pour publier un snapshot
    double population(int provinceId) const;
    int provinceCount() const { return static_cast<int>(m_byProvince.size()); }

    double totalPopulation() const { return m_totalPopulation; }
    double maxProvincePopulation() const { return m_maxProvincePopulation; }
    double stability() const { return m_stability; } // 0..1 (fraction sans famine)

    // Diplomatie : opinion entre civilisations (-100 guerre .. +100 alliance).
    int civCount() const { return m_civCount; }
    const std::vector<float>& opinionMatrix() const { return m_opinion; }
    const std::vector<double>& civPopulations() const { return m_civPopulation; }
    const std::vector<double>& civTech() const { return m_civTech; }
    const std::vector<int>& civProvinceCounts() const { return m_civProvinceCount; }

    // Champ nuageux simule (cycle de l'eau) pour le rendu des nuages.
    int climateWidth() const { return m_climate.width(); }
    int climateHeight() const { return m_climate.height(); }
    const std::vector<float>& cloudField() const { return m_climate.cloudField(); }

    static const char* eraName(int era);
    static int eraForTech(double tech); // indice d'ere a partir des points de techno

    const std::deque<EventRecord>& events() const { return m_events; }

    // Sauvegarde / chargement de l'etat dynamique (JSON). La topologie des
    // provinces (biomes, voisins) est deterministe : seul l'etat vivant est
    // serialise puis reapplique sur les entites existantes.
    bool saveToFile(const std::string& path, int year) const;
    bool loadFromFile(const std::string& path, int& yearOut);

    static const char* biomeName(Biome b);
    static const char* eventName(EventType t);

private:
    entt::registry m_registry;
    std::vector<entt::entity> m_byProvince;  // index = id de province
    std::vector<entt::entity> m_inhabited;   // provinces non oceaniques
    std::vector<std::vector<int>> m_neighbors; // graphe d'adjacence (par id)
    std::vector<glm::vec3> m_provinceDir;    // direction (repere planete-fixe)

    double m_totalPopulation = 0.0;
    double m_maxProvincePopulation = 1.0;
    double m_stability = 1.0;

    int m_civCount = 0;
    std::vector<float> m_opinion;        // matrice civ x civ (a plat)
    std::vector<double> m_civPopulation; // population totale par civ
    std::vector<double> m_civTech;       // points de technologie par civ
    std::vector<int> m_civProvinceCount; // nombre de provinces par civ
    std::vector<char> m_warState;        // etat de guerre par paire (a plat)

    bool atWar(int civA, int civB) const {
        if (civA < 0 || civB < 0 || civA >= m_civCount || civB >= m_civCount) return false;
        return m_warState[civA * m_civCount + civB] != 0;
    }

    Climate m_climate;
    std::mt19937 m_rng{12345u};
    std::deque<EventRecord> m_events;

    void recomputeAggregates();
    void exchangeBetweenProvinces(double days); // commerce + migration
    void tickDiplomacy(double days, int year);
    void tickTech(double days);
    void tickExpansion(double days, int year); // colonisation + conquete
    void spawnEvents(double days, int year, double timeDays);
    void logEvent(int year, std::string text, int severity);
};

} // namespace wl
