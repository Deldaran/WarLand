#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <random>
#include <glm/vec2.hpp>

struct TileMap; // fwd

// Paramètres descriptifs d'un biome pour génération procédurale
struct BiomeGenConfig {
    float baseElevation = 0.f;   // altitude moyenne attendue
    float elevationVariance = 1.f; // variation relative
    float roughness = 1.f;       // fréquence du bruit
    float vegetationDensity = 0.5f; // densité décor (0..1)
    float featureChance = 0.1f;  // structures spéciales
};

struct BiomeGenResultSample {
    float height = 0.f; // hauteur générée
    // futur: moisture, temperature, etc.
};

class BiomeGenerator {
public:
    // Génère (ou récupère) une seed pour un biomeId et applique la génération sur une zone discrète
    static void EnsureBiomeSeed(TileMap& map, int biomeId, uint64_t globalSeed);
    static BiomeGenConfig DefaultConfigFor(const std::string& biomeName);
    static void GenerateHeightsForBiome(TileMap& map, int biomeId, const BiomeGenConfig& cfg);
};
