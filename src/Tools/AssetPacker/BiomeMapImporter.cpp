#include "BiomeMapImporter.h"
#include "../../../external/stb/stb_image.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>

using json = nlohmann::json;

static uint32_t pack(uint8_t r,uint8_t g,uint8_t b){return (r<<16)|(g<<8)|b;}

bool BiomeMapImporter::Import(const std::string& pngPath,
                              const std::vector<ColorToTile>& palette,
                              const std::string& outJsonPath,
                              const std::string& atlasImage,
                              ImportResult* out) {
    int w=0,h=0,n=0; unsigned char* data = stbi_load(pngPath.c_str(), &w, &h, &n, 3);
    if (!data) return false;

    out->map.width=w; out->map.height=h; out->map.tileSize=32; out->map.atlasImagePath = atlasImage; out->map.tiles.resize(w*h);

    // Build color map for quick lookup
    std::unordered_map<uint32_t,uint16_t> lut;
    lut.reserve(palette.size());
    for (auto& e : palette) lut[pack(e.r,e.g,e.b)] = e.tileId;

    for (int y=0; y<h; ++y) {
        for (int x=0; x<w; ++x) {
            int i = (y*w + x)*3;
            uint8_t r=data[i], g=data[i+1], b=data[i+2];
            uint32_t k = pack(r,g,b);
            auto it = lut.find(k);
            out->map.tiles[y*w + x] = (it==lut.end() ? 0 : it->second);
        }
    }

    if (!outJsonPath.empty()) {
        json j;
        j["width"] = w;
        j["height"] = h;
        j["tileSize"] = out->map.tileSize;
        if (!atlasImage.empty()) j["atlasImage"] = atlasImage;
        j["tiles"] = out->map.tiles;
        std::ofstream f(outJsonPath); if (f.is_open()) f << j.dump(2);
    }

    stbi_image_free(data);
    return true;
}
