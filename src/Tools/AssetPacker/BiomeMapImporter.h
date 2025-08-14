#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../../Engine/Rendering/GL/TileMap.h"

struct ColorToTile { uint8_t r,g,b; uint16_t tileId; };
struct ColorToCountry { uint8_t r,g,b; uint16_t countryId; };
struct ColorToPlaceType { uint8_t r,g,b; std::string type; };

struct ImportResult {
    TileMap map;
};

class BiomeMapImporter {
public:
    // Importe un PNG indexé par couleurs -> tileId (biomes) dans ImportResult.map
    static bool Import(const std::string& pngPath,
                       const std::vector<ColorToTile>& palette,
                       const std::string& outJsonPath,
                       const std::string& atlasImage,
                       ImportResult* out);
};

class WorldGenerator {
public:
    // Génère une carte à partir de 3 PNG et palettes; remplit map, places et countries.
    static bool Generate(const std::string& biomePng,
                         const std::vector<ColorToTile>& biomePalette,
                         const std::string& countriesPng,
                         const std::vector<ColorToCountry>& countryPalette,
                         const std::string& citiesPng,
                         const std::vector<ColorToPlaceType>& placePalette,
                         uint64_t worldSeed,
                         const std::string& atlasImage,
                         ImportResult* out);
};
