#pragma once

#include <cstdint>

namespace wl {

// Biomes deduits de l'elevation et de la latitude de la province.
enum class Biome : uint8_t {
    Ocean = 0,
    Desert,
    Grassland,
    Forest,
    Tundra,
    Mountain,
    Polar,
    Count
};

// Identite geographique et politique d'une province.
struct CProvince {
    int id = -1;
    int civ = -1;
    Biome biome = Biome::Ocean;
    bool ocean = true;
};

// Population vivante de la province.
struct CPopulation {
    double count = 0.0;
    double lastFoodBalance = 0.0; // bilan alimentaire du dernier tick (>=0 = ok)
};

// Stocks de ressources (unites abstraites).
struct CStock {
    double food = 0.0;
    double materials = 0.0;
    double energy = 0.0;
};

// Rendements du biome (par sqrt(population), donne une capacite de charge).
struct BiomeYield {
    double food;
    double materials;
    double energy;
};

// Types de chocs qui frappent les provinces.
enum class EventType : uint8_t {
    Drought = 0,    // secheresse : effondre la production de nourriture
    Epidemic,       // epidemie : mortalite continue
    BumperHarvest,  // recolte exceptionnelle : bonus de nourriture
    Count
};

// Affliction active sur une province (un choc en cours).
struct CAffliction {
    EventType type = EventType::Drought;
    double daysLeft = 0.0;
    double foodFactor = 1.0;       // multiplie la production de nourriture
    double mortalityPerDay = 0.0;  // pertes de population par jour
};

} // namespace wl
