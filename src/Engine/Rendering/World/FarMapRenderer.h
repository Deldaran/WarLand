#pragma once
#include <vector>
#include <cstdint>
#include <array> // added for std::array
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "../GL/TileMap.h"

// Rend la carte lointaine (niveau 1) optimisée: texture d'indices pays (R8), palette GPU, VBO routes/croix batchés.
class FarMapRenderer {
public:
    bool init(const TileMap* map); // build all static buffers
    void shutdown();
    void rebuild(const TileMap* map); // si la carte change (regen)

    void render(const TileMap* map, const glm::mat4& vp, float zoom);

    void setShowGrid(bool v) { showGrid_ = v; }
    bool showGrid() const { return showGrid_; }
    void setShowAdaptive(bool v) { showAdaptive_ = v; }
    bool showAdaptive() const { return showAdaptive_; }
    void setHeightShading(bool v) { heightShading_ = v; }
    bool heightShading() const { return heightShading_; }
    void setShowCountries(bool v){ showCountries_ = v; }
    bool showCountries() const { return showCountries_; }
    void setCountryAlpha(float a){ countryAlpha_ = a; }
    float countryAlpha() const { return countryAlpha_; }
    void setCountryNeutral(uint16_t id, bool neutral);
    bool isCountryNeutral(uint16_t id) const;

private:
    void buildCountryIndexTexture(const TileMap* map);
    void buildCountryPalette(const TileMap* map); // NOUVEAU: palette couleurs pays
    void buildPalette(const TileMap* map); // MAJ: dépend de la map pour couleurs biomes
    void buildRoadBuffers(const TileMap* map);
    void buildCrossesBuffer(const TileMap* map);
    void buildPolygonBuffer(const TileMap* map);
    void buildGridBuffer(const TileMap* map);
    void rebuildDynamicGrid(const TileMap* map, float zoom);
    void buildAdaptiveCellsBuffer(const TileMap* map);
    float computeNiceStep(float raw) const;

    // GPU objects
    unsigned int countryIndexTex_ = 0; // R8 indices (réutilisé pour pays: R16UI ID pays)
    unsigned int paletteUBO_ = 0;      // palette biomes vec4
    unsigned int countryPaletteUBO_ = 0; // NOUVEAU palette pays

    unsigned int quadVao_ = 0, quadVbo_ = 0, quadIbo_ = 0;
    unsigned int roadsVao_ = 0, roadsVbo_ = 0, roadsIbo_ = 0; // primitive restart line strips
    unsigned int crossesVao_ = 0, crossesVbo_ = 0; // GL_LINES batched crosses
    unsigned int polyVao_ = 0, polyVbo_ = 0; // NEW: triangulated polygons
    unsigned int gridVao_ = 0, gridVbo_ = 0; // NEW: grid lines
    unsigned int adaptiveVao_ = 0, adaptiveVbo_ = 0; // instanced adaptive cells

    unsigned int mapProgram_ = 0;   // index -> color
    unsigned int lineProgram_ = 0;  // lines (routes + crosses reuse + grid)
    unsigned int polyProgram_ = 0;  // NEW: polygon shader

    int texW_ = 0, texH_ = 0;
    int roadIndexCount_ = 0;
    int crossVertexCount_ = 0;
    int polyVertexCount_ = 0; // NEW
    int gridVertexCount_ = 0; // NEW
    int adaptiveInstanceCount_ = 0; // number of adaptive cells
    float lastGridStep_ = -1.f; // NEW
    std::vector<glm::vec3> countryColors_; // CPU palette
    bool showGrid_ = true; // (legacy, unused visually)
    bool showAdaptive_ = true; // now: show adaptive cell borders (fill always on)
    bool heightShading_ = false; // modulation couleur par hauteur
    bool showCountries_ = true;  // NOUVEAU overlay pays
    float countryAlpha_ = 0.72f; // NOUVEAU opacité blend (défaut demandé 0.72)
    std::array<uint32_t,8> neutralMask_ = { 0x00000002u,0,0,0,0,0,0,0 }; // id 1 neutre par défaut (bit1)
    unsigned int neutralMaskUBO_ = 0; // UBO binding=2
    bool neutralMaskDirty_ = true;    // upload needed (id 1 déjà neutre)
};
