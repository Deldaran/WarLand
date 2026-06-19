#include "renderer/Skybox.h"

#include <glad/gl.h>
#include <stb_image.h>

#include <array>
#include <iostream>

namespace wl {

namespace {
// Cube unite (36 sommets) pour echantillonner le cubemap par direction.
const float kCube[] = {
    -1, 1,-1,  -1,-1,-1,   1,-1,-1,   1,-1,-1,   1, 1,-1,  -1, 1,-1,
    -1,-1, 1,  -1,-1,-1,  -1, 1,-1,  -1, 1,-1,  -1, 1, 1,  -1,-1, 1,
     1,-1,-1,   1,-1, 1,   1, 1, 1,   1, 1, 1,   1, 1,-1,   1,-1,-1,
    -1,-1, 1,  -1, 1, 1,   1, 1, 1,   1, 1, 1,   1,-1, 1,  -1,-1, 1,
    -1, 1,-1,   1, 1,-1,   1, 1, 1,   1, 1, 1,  -1, 1, 1,  -1, 1,-1,
    -1,-1,-1,  -1,-1, 1,   1,-1,-1,   1,-1,-1,  -1,-1, 1,   1,-1, 1,
};
} // namespace

Skybox::~Skybox() {
    if (m_tex) glDeleteTextures(1, &m_tex);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

bool Skybox::load(const std::string& dir, const std::string& prefix) {
    // Faces du cubemap : suffixe du pack -> cible OpenGL correspondante.
    const std::array<std::pair<const char*, GLenum>, 6> faces = {{
        {"_RIGHT", GL_TEXTURE_CUBE_MAP_POSITIVE_X},
        {"_LEFT",  GL_TEXTURE_CUBE_MAP_NEGATIVE_X},
        {"_UP",    GL_TEXTURE_CUBE_MAP_POSITIVE_Y},
        {"_DOWN",  GL_TEXTURE_CUBE_MAP_NEGATIVE_Y},
        {"_FRONT", GL_TEXTURE_CUBE_MAP_POSITIVE_Z},
        {"_BACK",  GL_TEXTURE_CUBE_MAP_NEGATIVE_Z},
    }};

    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_tex);

    stbi_set_flip_vertically_on_load(0); // les cubemaps ne se retournent pas
    for (const auto& f : faces) {
        std::string path = dir + "/" + prefix + f.first + ".png";
        int w = 0, h = 0, n = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 3);
        if (!data) {
            std::cerr << "[Skybox] face introuvable: " << path << "\n";
            glDeleteTextures(1, &m_tex);
            m_tex = 0;
            return false;
        }
        glTexImage2D(f.second, 0, GL_SRGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCube), kCube, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return true;
}

void Skybox::draw() const {
    if (!m_tex) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_tex);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

} // namespace wl
