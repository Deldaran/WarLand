#include "TileAtlas.h"
#include <glad/glad.h>
// stb_image implementation moved to src/ThirdParty/stb_image.cpp
#include "../../../external/stb/stb_image.h"
#include <vector>

bool TileAtlas::load(const TileAtlasDesc& desc) {
    int n=0; unsigned char* data = stbi_load(desc.atlasImagePath.c_str(), &width_, &height_, &n, 4);
    if (!data) return false;
    if (!glTex_) glGenTextures(1, &glTex_);
    glBindTexture(GL_TEXTURE_2D, glTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return true;
}
