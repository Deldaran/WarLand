// TerrainNoise.h - génération procédurale de relief continu
#pragma once
#include <cstdint>
struct TileMap; // fwd
struct TerrainNoiseConfig {
    int   octaves = 9;                  // valeurs par défaut (classique FBM)
    float lacunarity = 2.07f;
    float gain = 0.68f;
    float baseFrequency = 0.00373f;     // fréquence du bruit de base
    float globalAmplitude = 0.68f;      // hauteur max (0..1) avant extrusion (multiplie landScale)
    float seaLevel = 0.48f;             // seuil : tout en dessous = 0
    int   blurPasses = 2;               // lissage box (0 = brut)
    float continentFrequency = 0.00018f;// optionnel (macro forme)
    float continentStrength = 0.0f;     // 0 = désactivé (plat)
    float ridgeStrength = 0.0f;         // pas de crêtes
    float slopeX = 0.0f;                // pente globale X
    float slopeY = 0.0f;                // pente globale Y
    bool  useBiomes = false;            // désactivé (pas de modulation spécifique)
    float mountainBoost = 3.0f;         // multiplicateur amplitude spécifique montagnes (si useBiomes)
};
namespace TerrainNoise {
    void Generate(TileMap& map, uint64_t seed, const TerrainNoiseConfig& cfg);
}
