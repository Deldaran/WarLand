#include "renderer/props/PropLayer.h"
#include "renderer/planet/PlanetMesh.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>
#include <stb_image.h>

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace wl {

namespace {

// Hash deterministe -> [0,1).
float hash(uint32_t a, uint32_t b) {
    uint32_t h = a * 374761393u + b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFFu) / float(0x1000000u);
}

constexpr int kSprite = 64; // taille des couches de la texture array

// Categories de props et leur taille de base (unites monde, rayon planete = 1).
enum Cat { GreenTree, DarkTree, GoldTree, OrangeTree, Palm,
           DeadTree, Bush, Rock, Mushroom, Sprout, CatCount };

struct CatDef {
    float size;                       // taille de base du sprite (hauteur monde)
    std::vector<std::string> files;   // sprites du pack pour cette categorie
};

// Selection curated dans le pack de l'utilisateur (assets/ressources/).
// Les arbres sont des feuillus a variantes saisonnieres : on les repartit par
// couleur (vert lush / sombre / dore / orange) selon le biome.
const std::array<CatDef, CatCount>& catalog() {
    static const std::array<CatDef, CatCount> c = {{
        /* GreenTree  */ {0.00110f, {"ME_Singles_Camping_48x48_Tree_1.png",
                                     "ME_Singles_Camping_48x48_Tree_2.png",
                                     "ME_Singles_Camping_48x48_Tree_8.png",
                                     "ME_Singles_Camping_48x48_Tree_14.png"}},
        /* DarkTree   */ {0.00115f, {"ME_Singles_Camping_48x48_Tree_73.png",
                                     "ME_Singles_Camping_48x48_Tree_74.png",
                                     "ME_Singles_Camping_48x48_Tree_97.png",
                                     "ME_Singles_Camping_48x48_Tree_98.png"}},
        /* GoldTree   */ {0.00104f, {"ME_Singles_Camping_48x48_Tree_46.png",
                                     "ME_Singles_Camping_48x48_Tree_47.png",
                                     "ME_Singles_Camping_48x48_Tree_48.png"}},
        /* OrangeTree */ {0.00106f, {"ME_Singles_Camping_48x48_Tree_4.png",
                                     "ME_Singles_Camping_48x48_Tree_5.png",
                                     "ME_Singles_Camping_48x48_Tree_19.png",
                                     "ME_Singles_Camping_48x48_Tree_20.png"}},
        /* Palm       */ {0.00130f, {"21_Beach_48x48_Palm_Tree.png",
                                     "ME_Singles_Swimming_Pool_48x48_Palm_1.png",
                                     "ME_Singles_Swimming_Pool_48x48_Palm_2.png",
                                     "ME_Singles_Swimming_Pool_48x48_Palm_3.png"}},
        /* DeadTree   */ {0.00098f, {"ME_Singles_Camping_48x48_Tree_Dead_1.png",
                                     "ME_Singles_Camping_48x48_Tree_Dead_2.png",
                                     "ME_Singles_Camping_48x48_Tree_Dead_Stick_1.png",
                                     "ME_Singles_Camping_48x48_Tree_Dead_Stick_2.png"}},
        /* Bush       */ {0.00070f, {"ME_Singles_Garden_48x48_Bush_1.png",
                                     "ME_Singles_Garden_48x48_Bush_2.png",
                                     "ME_Singles_Garden_48x48_Bush_10.png"}},
        /* Rock       */ {0.00060f, {"ME_Singles_Camping_48x48_Rock_1.png",
                                     "ME_Singles_Camping_48x48_Rock_2.png",
                                     "ME_Singles_Camping_48x48_Rock_3.png",
                                     "ME_Singles_Camping_48x48_Rock_4.png"}},
        /* Mushroom   */ {0.00045f, {"ME_Singles_Camping_48x48_Mushrooms_1.png",
                                     "ME_Singles_Camping_48x48_Mushrooms_2.png"}},
        /* Sprout     */ {0.00052f, {"ME_Singles_Camping_48x48_Sprout_1.png",
                                     "ME_Singles_Camping_48x48_Sprout_2.png"}},
    }};
    return c;
}

// Plage de couches (layers) occupee par chaque categorie dans la texture array.
struct Layout {
    std::array<int, CatCount> start{};
    std::array<int, CatCount> count{};
    int total = 0;
};

Layout computeLayout() {
    Layout L;
    int s = 0;
    for (int c = 0; c < CatCount; ++c) {
        L.start[c] = s;
        L.count[c] = int(catalog()[c].files.size());
        s += L.count[c];
    }
    L.total = s;
    return L;
}

} // namespace

PropLayer::~PropLayer() {
    if (m_tex) glDeleteTextures(1, &m_tex);
    if (m_instVbo) glDeleteBuffers(1, &m_instVbo);
    if (m_quadVbo) glDeleteBuffers(1, &m_quadVbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void PropLayer::buildTextureArray(const std::string& assetsDir) {
    Layout L = computeLayout();
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, kSprite, kSprite, L.total,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    std::vector<uint8_t> tile(size_t(kSprite) * kSprite * 4);
    const std::string base = assetsDir + "/assets/ressources/";

    for (int c = 0; c < CatCount; ++c) {
        const auto& files = catalog()[c].files;
        for (int k = 0; k < int(files.size()); ++k) {
            int layer = L.start[c] + k;
            int w = 0, h = 0, n = 0;
            unsigned char* data = stbi_load((base + files[k]).c_str(), &w, &h, &n, 4);
            if (!data) {
                std::cerr << "[Props] sprite introuvable: " << files[k] << "\n";
                std::fill(tile.begin(), tile.end(), 0); // couche vide
            } else {
                // Redimensionnement plus-proche-voisin vers kSprite x kSprite.
                for (int y = 0; y < kSprite; ++y) {
                    int sy = y * h / kSprite;
                    for (int x = 0; x < kSprite; ++x) {
                        int sx = x * w / kSprite;
                        const unsigned char* sp = data + (size_t(sy) * w + sx) * 4;
                        uint8_t* dp = &tile[(size_t(y) * kSprite + x) * 4];
                        dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
                    }
                }
                stbi_image_free(data);
            }
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, kSprite, kSprite, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, tile.data());
        }
    }

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void PropLayer::build(const PlanetMesh& mesh, uint32_t seed, const std::string& assetsDir) {
    buildTextureArray(assetsDir);
    const Layout L = computeLayout();

    // Choisit une categorie selon le biome (r in [0,1) pour le melange).
    auto pickCat = [](int e_hi, float lat, float forest, float dry, float r) -> int {
        if (e_hi)                              // hautes montagnes
            return r < 0.6f ? Rock : (r < 0.8f ? DeadTree : DarkTree);
        if (lat > 0.72f)                       // toundra / polaire
            return r < 0.55f ? Rock : DeadTree;
        if (lat > 0.5f)                        // taiga / boreal
            return r < 0.7f ? DarkTree : (r < 0.85f ? Sprout : Rock);
        if (dry > 0.5f && forest < 0.45f)      // ceinture seche subtropicale
            return r < 0.45f ? DeadTree : (r < 0.8f ? Rock : Bush);
        if (lat < 0.16f)                       // equatorial / tropical
            return r < 0.45f ? GreenTree : (r < 0.75f ? Palm : Bush);
        // tempere : feuillus saisonniers + sous-bois
        if (r < 0.34f) return GreenTree;
        if (r < 0.55f) return OrangeTree;
        if (r < 0.68f) return GoldTree;
        if (r < 0.86f) return Bush;
        return Mushroom;
    };

    const auto& dirs = mesh.directions();
    const auto& pos = mesh.positions();
    const auto& elev = mesh.elevations();

    std::vector<float> inst; // pos(3) + size(1) + layer(1)
    inst.reserve(dirs.size() * 5 * 4);
    const size_t kMaxInstances = 280000;

    for (size_t i = 0; i < dirs.size() && inst.size() / 5 < kMaxInstances; ++i) {
        float e = elev[i];
        if (e <= 0.01f) continue; // mer / cotes
        glm::vec3 d = dirs[i];
        float radius = glm::length(pos[i]);
        float lat = std::abs(d.y);
        int e_hi = e > 0.5f ? 1 : 0;

        // Repere tangent local pour disperser autour du sommet.
        glm::vec3 ref = std::abs(d.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 east = glm::normalize(glm::cross(ref, d));
        glm::vec3 north = glm::cross(d, east);

        // Couvert forestier (amas) et aridite (deux bruits independants).
        float forest = glm::perlin(d * 5.0f + glm::vec3(float(seed) * 0.013f)) * 0.5f + 0.5f;
        float dry = glm::perlin(d * 3.0f + glm::vec3(11.0f + float(seed) * 0.007f)) * 0.5f + 0.5f;

        // Densite de placement selon le biome (arbres petits -> forets denses).
        float baseProb;
        if (e_hi)               baseProb = 0.40f;
        else if (lat > 0.72f)   baseProb = 0.18f;
        else if (lat > 0.5f)    baseProb = 0.75f * forest;
        else if (dry > 0.5f && forest < 0.45f) baseProb = 0.22f;
        else if (lat < 0.16f)   baseProb = 0.85f * (0.4f + forest);
        else                    baseProb = 0.8f * forest + 0.18f;

        for (int k = 0; k < 10; ++k) {
            float r0 = hash(uint32_t(i), uint32_t(k * 17 + 1));
            if (r0 > baseProb) continue;
            int cat = pickCat(e_hi, lat, forest, dry,
                              hash(uint32_t(i), uint32_t(k * 13 + 2)));
            int layer = L.start[cat] +
                int(hash(uint32_t(i), uint32_t(k * 29 + 4)) * L.count[cat]) % std::max(1, L.count[cat]);

            // Jitter tangentiel reduit (sur les pentes, un gros jitter avec le
            // rayon du sommet ferait flotter/enfoncer le prop).
            float jx = (hash(uint32_t(i), uint32_t(k * 7 + 3)) - 0.5f) * 0.008f;
            float jy = (hash(uint32_t(i), uint32_t(k * 11 + 5)) - 0.5f) * 0.008f;
            glm::vec3 jd = glm::normalize(d + east * jx + north * jy);
            // Base legerement ENTERREE (-0.0004) : couvre le micro-relief de la
            // tessellation (+-0.0002) + le jitter -> le prop ne flotte jamais
            // (sa base d'herbe reste plantee dans le sol).
            glm::vec3 p = jd * (radius - 0.0004f);
            float s = catalog()[cat].size *
                      (0.75f + 0.5f * hash(uint32_t(i), uint32_t(k * 23 + 9)));
            inst.insert(inst.end(), {p.x, p.y, p.z, s, float(layer)});
        }
    }

    m_count = int(inst.size() / 5);

    // Quad de base : x in [-0.5,0.5] (largeur), y in [0,1] (ancre au sol).
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
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_count);
    glBindVertexArray(0);
}

} // namespace wl
