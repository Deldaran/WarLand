#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Simple CPU-side tile atlas descriptor and GPU uploader
struct TileRegion { // one sub-image in the atlas
    std::string name;
    int x=0, y=0, w=0, h=0; // pixel coords in atlas image
};

struct TileAtlasDesc {
    std::string atlasImagePath;  // path to atlas PNG
    std::vector<TileRegion> regions; // named rects
};

class TileAtlas {
public:
    bool load(const TileAtlasDesc& desc); // loads image, creates GL texture
    unsigned int textureId() const { return glTex_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    unsigned int glTex_ = 0;
    int width_ = 0, height_ = 0;
};
