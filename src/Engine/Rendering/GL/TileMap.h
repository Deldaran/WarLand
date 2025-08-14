#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/vec2.hpp>

struct Place {
    std::string name;
    std::string type; // ex: city, landmark, region
    int x = 0; // pixels (ou unités carte) depuis l'origine (discrete grid)
    int y = 0;
};

struct RoadSegment { int x, y; }; // point discret sur la grille
struct Road { std::vector<RoadSegment> points; };

// New: raw polygon vertex (float world space)
struct PolyVertex { float x,y; uint16_t country; };

struct CellPoly { uint32_t firstVertex = 0; uint16_t vertexCount = 0; uint16_t country = 0; };

struct AdaptiveCell { float x,y,w,h; uint16_t paletteIndex; float meanHeight; }; // adaptive political grid cell (world units)

struct CountryInfo { int id = 0; std::string name; float x = 0.f; float y = 0.f; }; // position représentative (centroïde)

// Minimal 2D tile map for far zoom (top-down world view)
struct TileMap {
    int width = 0;   // in tiles (discrete grid width for index texture fallback)
    int height = 0;  // in tiles
    int tileSize = 32; // pixels per tile on atlas
    std::string atlasImagePath; // optionnel: image associée
    std::vector<uint16_t> tiles; // index into atlas regions OR biome id raster (importer populates)
    std::vector<uint16_t> countries; // id de pays par tuile (facultatif)
    std::vector<uint32_t> countryColorsRGB; // couleur politique par countryId (packed 0xRRGGBB), index 0 réservé
    std::vector<Place> places;   // lieux nommés optionnels
    std::vector<Road> roads;     // routes (liste de points discrétisés)
    std::vector<std::string> biomeNames; // indexé par biomeId, pour debug (peut être vide si non fourni)
    std::vector<uint32_t> biomeColorsRGB; // couleur originale du biome (0xRRGGBB) indexé par biomeId
    std::vector<float> tileHeights; // hauteur normalisée par cellule raster (width*height)
    std::vector<uint16_t> paletteIndices; // palette index par pixel (0 deep,1 shallow, >=2 biome+2)
    std::vector<CountryInfo> countryInfos; // infos pays (id interne, nom, position)

    // New polygonal data (world space continuous coordinates)
    float worldMaxX = 0.f; // original Azgaar maxX
    float worldMaxY = 0.f; // original Azgaar maxY
    float kmPerUnit = 1.f; // conversion world unit -> km
    float landMinHeight=0.f, landMaxHeight=0.f, waterMinHeight=0.f, waterMaxHeight=0.f; // stats hauteurs pour shading
    std::vector<PolyVertex> polygonVertices; // flattened triangle list vertices (after triangulation)
    std::vector<CellPoly> cellPolys; // optional per-cell polygon metadata (fan source)
    std::vector<AdaptiveCell> adaptiveCells; // adaptive coarse cells for L1 political rendering

    bool loadFromFile(const std::string& path); // parses a simple text or json map
};
