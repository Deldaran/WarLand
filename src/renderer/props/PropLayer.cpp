#include "renderer/props/PropLayer.h"
#include "renderer/planet/PlanetMesh.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>
#include <stb_image.h>

#include <vector>
#include <cmath>
#include <array>

namespace wl {

namespace {

// Hash deterministe -> [0,1) a partir de deux entiers.
float hash(uint32_t a, uint32_t b) {
    uint32_t h = a * 374761393u + b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFFu) / float(0x1000000u);
}

constexpr int kTile = 128;                  // taille d'une tuile de l'atlas
constexpr int kCols = 3;                    // 3x3 tuiles
constexpr int kAtlasW = kTile * kCols;      // 384

inline void px(std::vector<uint8_t>& img, int gx, int gy,
               uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (gx < 0 || gy < 0 || gx >= kAtlasW || gy >= kAtlasW) return;
    size_t i = (size_t(gy) * kAtlasW + gx) * 4;
    img[i] = r; img[i + 1] = g; img[i + 2] = b; img[i + 3] = a;
}

// Remplit une tuile avec un sprite placeholder dessine a la main.
// (x,y) local 0..127, y=0 en haut de la tuile = haut du sprite.
void drawPlaceholder(std::vector<uint8_t>& img, int type) {
    int tx = (type % kCols) * kTile;
    int ty = (type / kCols) * kTile;
    auto P = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        // legere variation pour casser l'aplat
        float n = hash(uint32_t(x + 7), uint32_t(y + type * 31)) * 0.18f - 0.09f;
        auto v = [&](uint8_t c) {
            int o = int(c * (1.0f + n));
            return uint8_t(o < 0 ? 0 : o > 255 ? 255 : o);
        };
        px(img, tx + x, ty + y, v(r), v(g), v(b), 255);
    };
    auto disc = [&](int cx, int cy, int rx, int ry,
                    uint8_t r, uint8_t g, uint8_t b) {
        for (int y = cy - ry; y <= cy + ry; ++y)
            for (int x = cx - rx; x <= cx + rx; ++x) {
                float dx = (x - cx) / float(rx), dy = (y - cy) / float(ry);
                if (dx * dx + dy * dy <= 1.0f) P(x, y, r, g, b);
            }
    };
    auto rect = [&](int x0, int y0, int x1, int y1,
                    uint8_t r, uint8_t g, uint8_t b) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) P(x, y, r, g, b);
    };

    switch (type) {
        case PropLayer::Arbre: // feuillu : tronc + houppier rond
            rect(60, 78, 68, 124, 96, 64, 36);
            disc(64, 52, 40, 42, 46, 120, 52);
            disc(48, 64, 24, 24, 40, 108, 46);
            disc(82, 62, 24, 24, 52, 128, 58);
            break;
        case PropLayer::Sapin: // conifere : tronc + etages triangulaires
            rect(61, 96, 67, 124, 90, 60, 34);
            for (int s = 0; s < 3; ++s) {
                int top = 14 + s * 30, base = top + 42, half = 18 + s * 14;
                for (int y = top; y <= base; ++y) {
                    float t = (y - top) / float(base - top);
                    int w = int(half * t);
                    for (int x = 64 - w; x <= 64 + w; ++x) P(x, y, 34, 96, 52);
                }
            }
            break;
        case PropLayer::Buisson: // buisson : amas de boules vertes
            disc(64, 96, 34, 30, 56, 122, 58);
            disc(46, 100, 22, 20, 48, 112, 50);
            disc(82, 100, 22, 20, 50, 118, 54);
            break;
        case PropLayer::Cactus: // cactus : tige + deux bras
            rect(58, 48, 70, 124, 42, 110, 62);
            rect(40, 78, 58, 90, 42, 110, 62);
            rect(40, 64, 50, 90, 42, 110, 62);
            rect(70, 70, 88, 82, 42, 110, 62);
            rect(78, 56, 88, 82, 42, 110, 62);
            break;
        case PropLayer::Rocher: // rocher : galet gris
            disc(64, 98, 42, 28, 118, 116, 120);
            disc(52, 92, 18, 14, 138, 136, 140);
            rect(24, 116, 104, 124, 96, 94, 98);
            break;
        case PropLayer::Herbe: // touffe : quelques brins
            for (int s = 0; s < 7; ++s) {
                int bx = 40 + s * 8;
                int topY = 84 + (s % 3) * 8;
                int lean = (s % 2 ? 1 : -1) * (3 + s % 4);
                for (int y = topY; y <= 124; ++y) {
                    float t = (y - topY) / float(124 - topY);
                    int x = bx + int(lean * (1.0f - t));
                    P(x, y, 74, 142, 60);
                    P(x + 1, y, 70, 134, 56);
                }
            }
            break;
        default: break;
    }
}

// Charge un PNG (RGBA) et le redimensionne (plus proche voisin) dans une tuile.
bool overlayPng(std::vector<uint8_t>& img, int type, const std::string& path) {
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) return false;
    int tx = (type % kCols) * kTile;
    int ty = (type / kCols) * kTile;
    for (int y = 0; y < kTile; ++y) {
        int sy = y * h / kTile;
        for (int x = 0; x < kTile; ++x) {
            int sx = x * w / kTile;
            const unsigned char* s = data + (size_t(sy) * w + sx) * 4;
            size_t d = (size_t(ty + y) * kAtlasW + (tx + x)) * 4;
            img[d] = s[0]; img[d + 1] = s[1]; img[d + 2] = s[2]; img[d + 3] = s[3];
        }
    }
    stbi_image_free(data);
    return true;
}

} // namespace

PropLayer::~PropLayer() {
    if (m_tex) glDeleteTextures(1, &m_tex);
    if (m_instVbo) glDeleteBuffers(1, &m_instVbo);
    if (m_quadVbo) glDeleteBuffers(1, &m_quadVbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void PropLayer::buildAtlas(const std::string& assetsDir) {
    std::vector<uint8_t> img(size_t(kAtlasW) * kAtlasW * 4, 0); // transparent
    for (int t = 0; t <= Herbe; ++t) drawPlaceholder(img, t);

    // Les PNG fournis par l'utilisateur ecrasent les placeholders.
    const std::array<std::pair<int, const char*>, 6> files = {{
        {Arbre, "arbre"}, {Sapin, "sapin"}, {Buisson, "buisson"},
        {Cactus, "cactus"}, {Rocher, "rocher"}, {Herbe, "herbe"},
    }};
    for (auto& f : files)
        overlayPng(img, f.first, assetsDir + "/assets/textures/props/" + f.second + ".png");

    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasW, kAtlasW, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PropLayer::build(const PlanetMesh& mesh, uint32_t seed, const std::string& assetsDir) {
    buildAtlas(assetsDir);

    const auto& dirs = mesh.directions();
    const auto& pos = mesh.positions();
    const auto& elev = mesh.elevations();

    // Instances : pos(3) + size(1) + type(1) = 5 floats.
    std::vector<float> inst;
    inst.reserve(dirs.size() * 5 * 2);
    const size_t kMaxInstances = 60000;

    for (size_t i = 0; i < dirs.size() && inst.size() / 5 < kMaxInstances; ++i) {
        float e = elev[i];
        if (e <= 0.01f) continue; // mer / cotes : pas de props
        glm::vec3 d = dirs[i];
        float radius = glm::length(pos[i]);
        float lat = std::abs(d.y); // 0 a l'equateur, 1 aux poles

        // Repere tangent local pour disperser autour du sommet.
        glm::vec3 ref = std::abs(d.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 east = glm::normalize(glm::cross(ref, d));
        glm::vec3 north = glm::cross(d, east);

        // Densite de couvert : bruit basse frequence -> forets en amas.
        float forest = glm::perlin(d * 5.0f + glm::vec3(float(seed) * 0.013f)) * 0.5f + 0.5f;

        // Choix du biome -> type dominant + densite.
        int type; float baseProb; float size;
        if (e > 0.45f) {                 // montagnes
            type = PropLayer::Rocher; baseProb = 0.35f; size = 0.0055f;
        } else if (lat > 0.72f) {        // toundra / polaire
            type = PropLayer::Rocher; baseProb = 0.12f; size = 0.004f;
        } else if (lat > 0.50f) {        // taiga froide
            type = PropLayer::Sapin; baseProb = 0.45f * forest; size = 0.0075f;
        } else if (lat > 0.28f && lat < 0.42f && forest < 0.45f) { // ceinture seche
            type = PropLayer::Cactus; baseProb = 0.10f; size = 0.005f;
        } else if (lat < 0.18f) {        // equatorial / jungle
            type = PropLayer::Arbre; baseProb = 0.55f * (0.4f + forest); size = 0.0085f;
        } else {                         // tempere
            type = (forest > 0.5f) ? PropLayer::Arbre : PropLayer::Buisson;
            baseProb = 0.5f * forest + 0.1f; size = 0.0075f;
        }

        // Plusieurs tentatives de placement par sommet (densite naturelle).
        for (int k = 0; k < 4; ++k) {
            float r0 = hash(uint32_t(i), uint32_t(k * 17 + 1));
            if (r0 > baseProb) continue;
            float jx = (hash(uint32_t(i), uint32_t(k * 7 + 3)) - 0.5f) * 0.018f;
            float jy = (hash(uint32_t(i), uint32_t(k * 11 + 5)) - 0.5f) * 0.018f;
            glm::vec3 jd = glm::normalize(d + east * jx + north * jy);
            glm::vec3 p = jd * radius;
            float s = size * (0.7f + 0.6f * hash(uint32_t(i), uint32_t(k * 23 + 9)));
            // Type secondaire occasionnel (herbe sous les arbres).
            int t = type;
            if (type == PropLayer::Arbre && r0 < baseProb * 0.35f) t = PropLayer::Herbe;
            inst.insert(inst.end(), {p.x, p.y, p.z, s, float(t)});
        }
    }

    m_count = int(inst.size() / 5);

    // Quad de base : x in [-0.5,0.5] (largeur), y in [0,1] (ancre au sol).
    // Attributs : aQuad(vec2) + aUV(vec2). v inverse (0 en haut de l'image).
    const float quad[] = {
        -0.5f, 0.0f, 0.0f, 1.0f,
         0.5f, 0.0f, 1.0f, 1.0f,
         0.5f, 1.0f, 1.0f, 0.0f,
        -0.5f, 0.0f, 0.0f, 1.0f,
         0.5f, 1.0f, 1.0f, 0.0f,
        -0.5f, 1.0f, 0.0f, 0.0f,
    };

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glGenBuffers(1, &m_instVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instVbo);
    glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(float),
                 inst.empty() ? nullptr : inst.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);
}

void PropLayer::draw() const {
    if (m_count <= 0) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_count);
    glBindVertexArray(0);
}

} // namespace wl
