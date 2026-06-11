#include "renderer/planet/PlanetMesh.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cmath>

namespace wl {

namespace {

// Bruit fractal (FBM) base sur le Perlin de GLM. Renvoie ~[-1, 1].
float fbm(glm::vec3 p, int octaves, float lacunarity, float gain) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * glm::perlin(p);
        norm += amp;
        amp *= gain;
        p *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

// Sommet intermediaire du mesh pendant la generation.
struct Vertex {
    glm::vec3 pos;     // position deplacee (avec relief)
    glm::vec3 normal;  // recalculee a partir du relief
    float elevation;   // [-1, 1]
};

// Cle d'arete pour partager les points milieux lors de la subdivision.
uint64_t edgeKey(uint32_t a, uint32_t b) {
    uint32_t lo = a < b ? a : b;
    uint32_t hi = a < b ? b : a;
    return (static_cast<uint64_t>(lo) << 32) | hi;
}

} // namespace

PlanetMesh::PlanetMesh(const Params& params) {
    generate(params);
}

PlanetMesh::~PlanetMesh() {
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void PlanetMesh::generate(const Params& params) {
    // --- 1. Icosaedre de base (12 sommets, 20 faces) ---
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<glm::vec3> dirs = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
    };
    for (auto& d : dirs) d = glm::normalize(d);

    std::vector<glm::uvec3> faces = {
        {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
        {1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
        {3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
        {4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1},
    };

    // --- 2. Subdivision avec cache des points milieux ---
    for (int level = 0; level < params.subdivisions; ++level) {
        std::unordered_map<uint64_t, uint32_t> midCache;
        std::vector<glm::uvec3> newFaces;
        newFaces.reserve(faces.size() * 4);

        auto midpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
            uint64_t key = edgeKey(a, b);
            auto it = midCache.find(key);
            if (it != midCache.end()) return it->second;
            glm::vec3 m = glm::normalize((dirs[a] + dirs[b]) * 0.5f);
            uint32_t idx = static_cast<uint32_t>(dirs.size());
            dirs.push_back(m);
            midCache.emplace(key, idx);
            return idx;
        };

        for (const auto& f : faces) {
            uint32_t a = midpoint(f.x, f.y);
            uint32_t b = midpoint(f.y, f.z);
            uint32_t c = midpoint(f.z, f.x);
            newFaces.push_back({f.x, a, c});
            newFaces.push_back({f.y, b, a});
            newFaces.push_back({f.z, c, b});
            newFaces.push_back({a, b, c});
        }
        faces.swap(newFaces);
    }

    // --- 3. Relief : deplacement de chaque sommet le long de sa normale ---
    glm::vec3 seedOffset(static_cast<float>(params.seed) * 0.1234f,
                         static_cast<float>(params.seed) * 0.7654f,
                         static_cast<float>(params.seed) * 0.4567f);

    std::vector<Vertex> vertices(dirs.size());
    for (size_t i = 0; i < dirs.size(); ++i) {
        glm::vec3 d = dirs[i];
        float e = fbm(d * params.baseFrequency + seedOffset, params.octaves, 2.0f, 0.5f);
        // Les oceans sont aplatis au niveau de la mer ; seules les terres montent.
        float land = glm::max(e, params.seaLevel);
        float radius = 1.0f + land * params.amplitude;
        vertices[i].pos = d * radius;
        vertices[i].normal = glm::vec3(0.0f);
        vertices[i].elevation = e;
    }

    // --- 4. Normales lissees a partir des faces deplacees ---
    for (const auto& f : faces) {
        glm::vec3& p0 = vertices[f.x].pos;
        glm::vec3& p1 = vertices[f.y].pos;
        glm::vec3& p2 = vertices[f.z].pos;
        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        vertices[f.x].normal += n;
        vertices[f.y].normal += n;
        vertices[f.z].normal += n;
    }
    for (auto& v : vertices) {
        v.normal = glm::length(v.normal) > 1e-6f ? glm::normalize(v.normal)
                                                 : glm::normalize(v.pos);
    }

    // --- 5. Mise a plat pour le GPU : pos(3) + normal(3) + elevation(1) ---
    std::vector<float> buffer;
    buffer.reserve(vertices.size() * 7);
    for (const auto& v : vertices) {
        buffer.insert(buffer.end(), {v.pos.x, v.pos.y, v.pos.z,
                                     v.normal.x, v.normal.y, v.normal.z,
                                     v.elevation});
    }
    std::vector<uint32_t> indices;
    indices.reserve(faces.size() * 3);
    for (const auto& f : faces) {
        indices.push_back(f.x);
        indices.push_back(f.y);
        indices.push_back(f.z);
    }

    m_vertexCount = static_cast<int>(vertices.size());
    m_indexCount = static_cast<int>(indices.size());

    // --- 6. Upload OpenGL ---
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(float), buffer.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = 7 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void PlanetMesh::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace wl
