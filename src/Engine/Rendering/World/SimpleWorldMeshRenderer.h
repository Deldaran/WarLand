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
	void setHeightScale(float s){ heightScale_ = s; }
	float heightScale() const { return heightScale_; }

	// Adaptive refinement near camera
	void setCamera(float x, float y){ camX_ = x; camY_ = y; }
	void setAdaptive(bool enabled, float radiusUnits, int refineFactor, int outerStep){ adaptiveEnabled_ = enabled; adaptiveRadius_ = radiusUnits; refineFactor_ = refineFactor; outerStep_ = outerStep; needsRebuild_ = true; }

private:
	// no palette/colors in minimal viewer
	void buildMesh(const TileMap* map);
	void buildAdaptiveMesh(const TileMap* map);
	void ensureProgram();

private:
	unsigned int vao_ = 0, vbo_ = 0, ibo_ = 0;
	unsigned int program_ = 0;    // shader mesh
	int indexCount_ = 0;
	// Tessellation buffers
	unsigned int vaoT_ = 0, vboT_ = 0, iboT_ = 0;
	int indexCountT_ = 0;
	bool heightShading_ = true;
	float heightScale_ = 20.0f; // Z scale for aH (default)
	// Stats build
	int builtWidth_ = 0, builtHeight_ = 0;
	// Adaptive params (CPU-based fallback)
	bool adaptiveEnabled_ = false; float adaptiveRadius_ = 120.f; int refineFactor_ = 4; int outerStep_ = 2; bool needsRebuild_ = false; float camX_ = 0.f, camY_ = 0.f;

	// GPU tessellation
	public:
	void setTessEnabled(bool v){ useTess_ = v; needsRebuild_ = true; }
	bool tessEnabled() const { return useTess_; }
	void setTessParams(float nearDist, float farDist, int minLevel, int maxLevel, int baseStep){ tessNear_=nearDist; tessFar_=farDist; tessMin_=minLevel; tessMax_=maxLevel; tessBaseStep_=baseStep; needsRebuild_=true; }

private:
	void ensureProgramTess();
	void buildTessGrid(const TileMap* map);
	void uploadHeightTex(const TileMap* map);

	unsigned int programTess_ = 0; // tessellation pipeline program
	unsigned int heightTex_ = 0; int hmW_ = 0, hmH_ = 0; // heightmap texture
	bool useTess_ = true; float tessNear_ = 250.f; float tessFar_ = 3000.f; int tessMin_ = 1; int tessMax_ = 16; int tessBaseStep_ = 8;
};
