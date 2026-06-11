#include "renderer/overlay/ProvinceMap.h"
#include "renderer/planet/PlanetMesh.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <vector>
#include <cmath>

namespace wl {

namespace {

// Repartition uniforme de N points sur la sphere (spirale de Fibonacci).
std::vector<glm::vec3> fibonacciSphere(int n, float angleOffset) {
    std::vector<glm::vec3> pts(n);
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f)); // angle d'or
    for (int i = 0; i < n; ++i) {
        float y = 1.0f - 2.0f * (i + 0.5f) / static_cast<float>(n);
        float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        float theta = golden * i + angleOffset;
        pts[i] = glm::vec3(r * std::cos(theta), y, r * std::sin(theta));
    }
    return pts;
}

// HSV -> RGB (h, s, v dans [0,1]) pour generer des couleurs de civ distinctes.
glm::vec3 hsv2rgb(float h, float s, float v) {
    float i = std::floor(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (static_cast<int>(i) % 6) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}

// Index de l'element le plus proche (angle min = produit scalaire max).
int nearestSeed(const glm::vec3& dir, const std::vector<glm::vec3>& seeds) {
    int best = 0;
    float bestDot = -2.0f;
    for (size_t i = 0; i < seeds.size(); ++i) {
        float d = glm::dot(dir, seeds[i]);
        if (d > bestDot) { bestDot = d; best = static_cast<int>(i); }
    }
    return best;
}

} // namespace

ProvinceMap::ProvinceMap(const PlanetMesh& planet, const Params& params) {
    build(planet, params);
}

ProvinceMap::~ProvinceMap() {
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void ProvinceMap::build(const PlanetMesh& planet, const Params& params) {
    m_provinceCount = params.provinces;
    m_civCount = params.civs;

    float seedAngle = static_cast<float>(params.seed) * 0.0011f;

    // Graines de provinces et capitales de civilisations.
    std::vector<glm::vec3> provinceSeeds = fibonacciSphere(params.provinces, seedAngle);
    std::vector<glm::vec3> civCapitals = fibonacciSphere(params.civs, seedAngle + 1.3f);

    // Couleur par civilisation (teinte repartie sur la roue chromatique).
    std::vector<glm::vec3> civColors(params.civs);
    for (int c = 0; c < params.civs; ++c) {
        float h = static_cast<float>(c) / static_cast<float>(params.civs);
        civColors[c] = hsv2rgb(h, 0.55f, 0.85f);
    }

    // Chaque province appartient a la civilisation dont la capitale est la plus
    // proche -> nations contigues.
    std::vector<int> provinceCiv(params.provinces);
    for (int p = 0; p < params.provinces; ++p) {
        provinceCiv[p] = nearestSeed(provinceSeeds[p], civCapitals);
    }

    // Assignation de chaque sommet du globe a sa province -> couleur de civ.
    const auto& dirs = planet.directions();
    const auto& positions = planet.positions();
    const auto& indices = planet.indices();

    std::vector<float> buffer;
    buffer.reserve(dirs.size() * 6); // pos(3) + color(3)
    for (size_t i = 0; i < dirs.size(); ++i) {
        int prov = nearestSeed(dirs[i], provinceSeeds);
        glm::vec3 color = civColors[provinceCiv[prov]];
        glm::vec3 p = positions[i] * 1.002f; // legerement au-dessus du terrain
        buffer.insert(buffer.end(), {p.x, p.y, p.z, color.r, color.g, color.b});
    }

    m_indexCount = static_cast<int>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(float), buffer.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                 indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = 6 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void ProvinceMap::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace wl
