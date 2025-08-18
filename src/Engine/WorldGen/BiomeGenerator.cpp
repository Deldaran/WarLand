#include "BiomeGenerator.h"
#include "../Rendering/GL/TileMap.h"
#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

// Simple valeur bruit pseudo (hash)
static float HashNoise(int x, int y, uint64_t seed) {
    uint64_t h = seed;
    h ^= (uint64_t)x * 0x9E3779B185EBCA87ull;
    h ^= (uint64_t)y * 0xC2B2AE3D27D4EB4Full;
    h ^= (h >> 33); h *= 0xff51afd7ed558ccdull; h ^= (h >> 33); h *= 0xc4ceb9fe1a85ec53ull; h ^= (h >> 33);
    // Map to [0,1]
    return (float)(h & 0xFFFFFFFFull) / 4294967295.0f;
}

void BiomeGenerator::EnsureBiomeSeed(TileMap& map, int biomeId, uint64_t globalSeed) {
    if (biomeId < 0) return;
    if ((size_t)biomeId >= map.biomeSeeds.size()) map.biomeSeeds.resize(biomeId+1, 0);
    if (map.biomeSeeds[biomeId] == 0) {
        // dérive une seed stable à partir du global + biomeId + nom
        uint64_t s = globalSeed ^ (uint64_t)(biomeId * 0x9E3779B1);
        if ((size_t)biomeId < map.biomeNames.size()) {
            for (char c : map.biomeNames[biomeId]) {
                s = (s * 1315423911u) + (uint8_t)c;
            }
        }
        if (s == 0) s = 0xA5A5A5A5A5ull + biomeId;
        map.biomeSeeds[biomeId] = s;
    }
}

BiomeGenConfig BiomeGenerator::DefaultConfigFor(const std::string& biomeName) {
    BiomeGenConfig cfg;
    std::string n = biomeName; std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    if (n.find("mountain") != std::string::npos || n.find("montagne") != std::string::npos) {
        cfg.baseElevation = 0.7f; cfg.elevationVariance = 0.5f; cfg.roughness = 2.5f; cfg.vegetationDensity = 0.3f; cfg.featureChance = 0.25f;
    } else if (n.find("desert") != std::string::npos || n.find("desert") != std::string::npos) {
        cfg.baseElevation = 0.25f; cfg.elevationVariance = 0.15f; cfg.roughness = 1.2f; cfg.vegetationDensity = 0.05f; cfg.featureChance = 0.05f;
    } else if (n.find("plaine") != std::string::npos || n.find("plain") != std::string::npos || n.find("grass") != std::string::npos) {
        cfg.baseElevation = 0.35f; cfg.elevationVariance = 0.08f; cfg.roughness = 0.8f; cfg.vegetationDensity = 0.7f; cfg.featureChance = 0.08f;
    } else if (n.find("forest") != std::string::npos || n.find("forêt") != std::string::npos) {
        cfg.baseElevation = 0.40f; cfg.elevationVariance = 0.10f; cfg.roughness = 1.0f; cfg.vegetationDensity = 0.85f; cfg.featureChance = 0.12f;
    } else if (n.find("tundra") != std::string::npos) {
        cfg.baseElevation = 0.30f; cfg.elevationVariance = 0.12f; cfg.roughness = 1.3f; cfg.vegetationDensity = 0.2f; cfg.featureChance = 0.06f;
    } else if (n.find("swamp") != std::string::npos || n.find("marais") != std::string::npos) {
        cfg.baseElevation = 0.20f; cfg.elevationVariance = 0.05f; cfg.roughness = 0.9f; cfg.vegetationDensity = 0.9f; cfg.featureChance = 0.10f;
    } else {
        // défaut
        cfg.baseElevation = 0.4f; cfg.elevationVariance = 0.1f; cfg.roughness = 1.0f; cfg.vegetationDensity = 0.5f; cfg.featureChance = 0.1f;
    }
    return cfg;
}

void BiomeGenerator::GenerateHeightsForBiome(TileMap& map, int biomeId, const BiomeGenConfig& cfg) {
    if (biomeId < 0) return;
    if (map.width <=0 || map.height<=0) return;
    if (map.tileHeights.size() != (size_t)map.width * map.height) map.tileHeights.resize((size_t)map.width * map.height, 0.f);
    if ((size_t)biomeId >= map.biomeSeeds.size()) return; // EnsureBiomeSeed non appelé
    uint64_t seed = map.biomeSeeds[biomeId];
    if (seed == 0) return;

    // Parcours des tiles et applique un bruit simple
    for (int y=0; y<map.height; ++y) {
        for (int x=0; x<map.width; ++x) {
            size_t idx = (size_t)y * map.width + x;
            if (idx < map.paletteIndices.size()) {
                uint16_t pal = map.paletteIndices[idx];
                int bId = (pal >= 2) ? (int)pal - 2 : -1; // -1 eau
                if (bId == biomeId) {
                    // échantillonnage multi-octaves simple
                    float h = 0.f; float sumW=0.f;
                    float freq = cfg.roughness; float amp = 1.f;
                    for (int o=0; o<4; ++o) {
                        int nx = (int)std::floor(x * freq);
                        int ny = (int)std::floor(y * freq);
                        float n = HashNoise(nx, ny, seed + (uint64_t)o*0x9E37ull);
                        h += n * amp; sumW += amp; amp *= 0.5f; freq *= 2.0f;
                    }
                    if (sumW>0) h/=sumW;
                    // centre autour de baseElevation puis applique variance
                    h = cfg.baseElevation + (h - 0.5f) * cfg.elevationVariance;
                    h = std::clamp(h, 0.f, 1.f);
                    map.tileHeights[idx] = h;
                }
            }
        }
    }
}
