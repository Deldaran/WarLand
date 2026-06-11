#include "renderer/overlay/ProvinceMap.h"
#include "renderer/planet/PlanetMesh.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <vector>
#include <unordered_set>
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
    m_provinceSeeds = fibonacciSphere(params.provinces, seedAngle);
    std::vector<glm::vec3> civCapitals = fibonacciSphere(params.civs, seedAngle + 1.3f);

    // Couleur par civilisation (teinte repartie sur la roue chromatique).
    m_civColors.resize(params.civs);
    for (int c = 0; c < params.civs; ++c) {
        float h = static_cast<float>(c) / static_cast<float>(params.civs);
        m_civColors[c] = hsv2rgb(h, 0.55f, 0.85f);
    }

    // Chaque province appartient a la civilisation dont la capitale est la plus
    // proche -> nations contigues.
    m_provinceCiv.resize(params.provinces);
    for (int p = 0; p < params.provinces; ++p) {
        m_provinceCiv[p] = nearestSeed(m_provinceSeeds[p], civCapitals);
    }

    // Assignation de chaque sommet du globe a sa province + agregation
    // geographique (elevation, latitude) pour deduire les biomes.
    const auto& dirs = planet.directions();
    const auto& positions = planet.positions();
    const auto& elevations = planet.elevations();
    const auto& indices = planet.indices();

    m_vertexProvince.resize(dirs.size());
    m_overlayPositions.resize(dirs.size());

    std::vector<double> elevSum(params.provinces, 0.0);
    std::vector<double> latSum(params.provinces, 0.0);
    std::vector<int> count(params.provinces, 0);

    for (size_t i = 0; i < dirs.size(); ++i) {
        int prov = nearestSeed(dirs[i], m_provinceSeeds);
        m_vertexProvince[i] = prov;
        m_overlayPositions[i] = positions[i] * 1.002f; // legerement au-dessus du terrain
        elevSum[prov] += elevations[i];
        latSum[prov] += std::abs(dirs[i].y);
        count[prov] += 1;
    }

    m_provinceElevation.resize(params.provinces);
    m_provinceLatitude.resize(params.provinces);
    for (int p = 0; p < params.provinces; ++p) {
        int n = count[p] > 0 ? count[p] : 1;
        m_provinceElevation[p] = static_cast<float>(elevSum[p] / n);
        m_provinceLatitude[p] = static_cast<float>(latSum[p] / n);
    }

    // Graphe d'adjacence : deux provinces sont voisines si une arete de
    // triangle relie un sommet de l'une a un sommet de l'autre.
    std::vector<std::unordered_set<int>> adj(params.provinces);
    auto addEdge = [&](int a, int b) {
        int pa = m_vertexProvince[a];
        int pb = m_vertexProvince[b];
        if (pa != pb) { adj[pa].insert(pb); adj[pb].insert(pa); }
    };
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        int v0 = indices[i], v1 = indices[i + 1], v2 = indices[i + 2];
        addEdge(v0, v1);
        addEdge(v1, v2);
        addEdge(v2, v0);
    }
    m_neighbors.resize(params.provinces);
    for (int p = 0; p < params.provinces; ++p) {
        m_neighbors[p].assign(adj[p].begin(), adj[p].end());
    }

    m_indexCount = static_cast<int>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                 indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = 7 * sizeof(float);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Coloration politique initiale.
    applyPoliticalColors();
}

void ProvinceMap::uploadInterleaved(const std::vector<glm::vec3>& vertexColors) {
    std::vector<float> buffer;
    buffer.reserve(m_vertexProvince.size() * 7); // pos(3) + color(3) + provinceId(1)
    for (size_t i = 0; i < m_vertexProvince.size(); ++i) {
        const glm::vec3& p = m_overlayPositions[i];
        const glm::vec3& c = vertexColors[i];
        buffer.insert(buffer.end(), {p.x, p.y, p.z, c.r, c.g, c.b,
                                     static_cast<float>(m_vertexProvince[i])});
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(float), buffer.data(), GL_DYNAMIC_DRAW);
}

void ProvinceMap::setProvinceColors(const std::vector<glm::vec3>& provinceColors) {
    std::vector<glm::vec3> vertexColors(m_vertexProvince.size());
    for (size_t i = 0; i < m_vertexProvince.size(); ++i) {
        int prov = m_vertexProvince[i];
        vertexColors[i] = (prov < static_cast<int>(provinceColors.size()))
                              ? provinceColors[prov] : glm::vec3(1.0f);
    }
    uploadInterleaved(vertexColors);
}

void ProvinceMap::applyPoliticalColors() {
    std::vector<glm::vec3> provinceColors(m_provinceCount);
    for (int p = 0; p < m_provinceCount; ++p) {
        provinceColors[p] = m_civColors[m_provinceCiv[p]];
    }
    setProvinceColors(provinceColors);
}

float ProvinceMap::provinceElevation(int province) const {
    if (province < 0 || province >= static_cast<int>(m_provinceElevation.size())) return 0.0f;
    return m_provinceElevation[province];
}

float ProvinceMap::provinceLatitude(int province) const {
    if (province < 0 || province >= static_cast<int>(m_provinceLatitude.size())) return 0.0f;
    return m_provinceLatitude[province];
}

const std::vector<int>& ProvinceMap::neighbors(int province) const {
    static const std::vector<int> empty;
    if (province < 0 || province >= static_cast<int>(m_neighbors.size())) return empty;
    return m_neighbors[province];
}

void ProvinceMap::pick(const glm::vec3& dir, int& province, int& civ) const {
    province = nearestSeed(glm::normalize(dir), m_provinceSeeds);
    civ = m_provinceCiv[province];
}

glm::vec3 ProvinceMap::civColor(int civ) const {
    if (civ < 0 || civ >= static_cast<int>(m_civColors.size())) return glm::vec3(1.0f);
    return m_civColors[civ];
}

int ProvinceMap::provinceCiv(int province) const {
    if (province < 0 || province >= static_cast<int>(m_provinceCiv.size())) return -1;
    return m_provinceCiv[province];
}

void ProvinceMap::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace wl
