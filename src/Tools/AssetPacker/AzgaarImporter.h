#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../../Engine/Rendering/GL/TileMap.h"

struct AzgaarImportConfig {
    int targetWidth = 2000;   // taille grille interne (discrete fallback)
    int targetHeight = 2000;
    bool clampToMap = true;   // clamp coords dans la grille
    bool keepAzgaarNames = true; // sinon passer par générateur interne
    float worldKmWidth = 2700.f; // largeur monde en km (pour km grid)
};

struct AzgaarImportResult {
    TileMap map;                // tiles, countries, places remplis + polygons
    int sourceCellCount = 0;    // nombre de cellules Azgaar
    int skippedCells = 0;       // cellules ignorées (hors range / invalides)
    int placedBurgs = 0;        // villes placées
};

// Conversion du JSON Azgaar (export "Map data") en TileMap interne.
// jsonPath: chemin du fichier .map ou .json exporté.
// atlasImage: chemin vers l'image d'atlas (stocké dans TileMap)
// worldSeed: seed pour noms générés si keepAzgaarNames=false
// Retourne true si succès.
namespace AzgaarImporter {
    bool Load(const std::string& jsonPath,
              const AzgaarImportConfig& cfg,
              const std::string& atlasImage,
              uint64_t worldSeed,
              AzgaarImportResult& out);
}
