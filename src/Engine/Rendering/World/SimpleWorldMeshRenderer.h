// SimpleWorldMeshRenderer
// Rendu niveau L2: maillage détaillé (une cellule par tuile) coloré par biome.
// Pour l'instant: génère un grid mesh (triangle strip dégénéré) avec palette biomes (256 entrées) + shading hauteur optionnel.
// NOTE: Pour des cartes très grandes (> ~2500x2500) la mémoire peut devenir importante (>= ~100MB). Dans ce cas on pourra
//       remplacer par un rendu par texture (index texture + quad) ou un maillage compressé (merging runs).

#pragma once
#include <cstdint>
#include <vector>
#include <glm/mat4x4.hpp>
#include "../GL/TileMap.h"

class SimpleWorldMeshRenderer {
public:
	bool init(const TileMap* map);
	void shutdown();
	void rebuild(const TileMap* map); // re-génère le mesh (si map changée)
	void render(const TileMap* map, const glm::mat4& vp, float zoom, bool force = false);

	void setHeightShading(bool v){ heightShading_ = v; }
	bool heightShading() const { return heightShading_; }

private:
	void buildPalette(const TileMap* map); // construit / met à jour UBO palette
	void buildMesh(const TileMap* map);
	void ensureProgram();

private:
	unsigned int vao_ = 0, vbo_ = 0, ibo_ = 0;
	unsigned int paletteUBO_ = 0; // binding=0
	unsigned int program_ = 0;    // shader mesh
	int indexCount_ = 0;
	bool heightShading_ = true;
	// Stats build
	int builtWidth_ = 0, builtHeight_ = 0;
};
