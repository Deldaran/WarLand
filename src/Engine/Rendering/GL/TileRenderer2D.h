#pragma once
#include <glm/mat4x4.hpp>
#include <string>

class TileAtlas; struct TileMap;

class TileRenderer2D {
public:
    bool init();
    void shutdown();

    // Render the map using the given atlas and a camera/view-projection
    void render(const TileMap& map, const TileAtlas& atlas, const glm::mat4& viewProj);
    void renderGrid(int cellsX, int cellsY, float cellSize, const glm::mat4& viewProj);

private:
    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ibo_ = 0;
    unsigned int gridProgram_ = 0;
};
