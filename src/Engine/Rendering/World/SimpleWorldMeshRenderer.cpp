// Implémentation SimpleWorldMeshRenderer
#include "SimpleWorldMeshRenderer.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdio>
#include <algorithm>

namespace {
static GLuint compile(GLenum t, const char* src){ GLuint s=glCreateShader(t); glShaderSource(s,1,&src,nullptr); glCompileShader(s); GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok); if(!ok){ char log[2048]; glGetShaderInfoLog(s,2048,nullptr,log); std::fprintf(stderr,"[L2Mesh][Shader] Compile error: %s\n", log);} return s; }
static GLuint link(GLuint vs, GLuint fs){ GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p); GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok); if(!ok){ char log[2048]; glGetProgramInfoLog(p,2048,nullptr,log); std::fprintf(stderr,"[L2Mesh][Shader] Link error: %s\n", log);} return p; }
}

bool SimpleWorldMeshRenderer::init(const TileMap* map){ buildPalette(map); buildMesh(map); ensureProgram(); return true; }
void SimpleWorldMeshRenderer::shutdown(){ if(ibo_) glDeleteBuffers(1,&ibo_); if(vbo_) glDeleteBuffers(1,&vbo_); if(vao_) glDeleteVertexArrays(1,&vao_); if(paletteUBO_) glDeleteBuffers(1,&paletteUBO_); if(program_) glDeleteProgram(program_); vao_=vbo_=ibo_=paletteUBO_=program_=0; indexCount_=0; }
void SimpleWorldMeshRenderer::rebuild(const TileMap* map){ buildPalette(map); buildMesh(map); }

void SimpleWorldMeshRenderer::buildPalette(const TileMap* map){ if(!paletteUBO_) glGenBuffers(1,&paletteUBO_); glBindBuffer(GL_UNIFORM_BUFFER,paletteUBO_); std::vector<float> packed(256*4,1.f); // RGBA
	// Remplir: 0 deep water,1 shallow water, >=2 biome palette (mêmes couleurs que FarMapRenderer)
	auto biomeRealColor=[&](size_t id)->uint32_t{ switch(id){case 0: return 0x0A3D66; case 1: return 0xD9C07C; case 2: return 0xE3D7C9; case 3: return 0xC7D25A; case 4: return 0x7DB654; case 5: return 0x2F6B2F; case 6: return 0x4D7D38; case 7: return 0x0F4D2E; case 8: return 0x1F5D46; case 9: return 0x285138; case 10: return 0x7C765A; case 11: return 0xBFE8F5; case 12: return 0x4B6D4D; default: return 0x707070; }};
	// Eau
	packed[0]=0.0f; packed[1]=0.19f; packed[2]=0.27f; // deep
	packed[4]=0.10f; packed[5]=0.34f; packed[6]=0.47f; // shallow
	if(map){ size_t nb = map->biomeColorsRGB.size(); for(size_t b=0;b<nb && b+2<256;++b){ uint32_t rgb = biomeRealColor(b); float r=((rgb>>16)&0xFF)/255.f; float g=((rgb>>8)&0xFF)/255.f; float bl=(rgb&0xFF)/255.f; packed[(b+2)*4+0]=r; packed[(b+2)*4+1]=g; packed[(b+2)*4+2]=bl; }}
	glBufferData(GL_UNIFORM_BUFFER, packed.size()*sizeof(float), packed.data(), GL_STATIC_DRAW); glBindBufferBase(GL_UNIFORM_BUFFER,0,paletteUBO_); }

void SimpleWorldMeshRenderer::buildMesh(const TileMap* map){ if(vao_){ glDeleteBuffers(1,&vbo_); glDeleteBuffers(1,&ibo_); glDeleteVertexArrays(1,&vao_); vao_=vbo_=ibo_=0; indexCount_=0; }
	if(!map||map->width<=1||map->height<=1) return; builtWidth_=map->width; builtHeight_=map->height;
	// Générer un grid mesh triangle strip par ligne (avec indices dégénérés) ou simple éléments.
	// Pour simplicité initiale: indices triangles explicites (2 tris par cellule) -> 6 indices * (w-1)*(h-1)
	const int w=map->width; const int h=map->height; const float sx = (w>1 && map->worldMaxX>0)? map->worldMaxX/(float)(w-1):1.f; const float sy = (h>1 && map->worldMaxY>0)? map->worldMaxY/(float)(h-1):1.f;
	struct V { float x,y; uint16_t pal; float height; }; std::vector<V> verts; verts.resize((size_t)w*h);
	for(int y=0;y<h;++y){ for(int x=0;x<w;++x){ size_t idx=(size_t)y*w+x; uint16_t pal = (idx < map->paletteIndices.size())? map->paletteIndices[idx]:0; float ht = (idx < map->tileHeights.size())? map->tileHeights[idx]:0.f; verts[idx] = { x*sx, y*sy, pal, ht }; }}
	std::vector<uint32_t> idxs; idxs.reserve((size_t)(w-1)*(h-1)*6);
	for(int y=0;y<h-1;++y){ for(int x=0;x<w-1;++x){ uint32_t i0=y*w+x; uint32_t i1=y*w+x+1; uint32_t i2=(y+1)*w+x; uint32_t i3=(y+1)*w+x+1; // two triangles i0,i2,i1 and i1,i2,i3
			idxs.push_back(i0); idxs.push_back(i2); idxs.push_back(i1); idxs.push_back(i1); idxs.push_back(i2); idxs.push_back(i3); }}
	glGenVertexArrays(1,&vao_); glBindVertexArray(vao_); glGenBuffers(1,&vbo_); glBindBuffer(GL_ARRAY_BUFFER,vbo_); glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(V), verts.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(V),(void*)0);
	glEnableVertexAttribArray(1); glVertexAttribIPointer(1,1,GL_UNSIGNED_SHORT,sizeof(V),(void*)(2*sizeof(float)));
	glEnableVertexAttribArray(2); glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,sizeof(V),(void*)(2*sizeof(float)+sizeof(uint16_t)));
	glGenBuffers(1,&ibo_); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo_); glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxs.size()*sizeof(uint32_t), idxs.data(), GL_STATIC_DRAW); indexCount_=(int)idxs.size(); glBindVertexArray(0);
	std::fprintf(stderr,"[L2Mesh] Built mesh %dx%d cells (%zu verts, %d indices) ~%.2f MB\n", w,h, verts.size(), indexCount_, (verts.size()*sizeof(V)+idxs.size()*sizeof(uint32_t))/ (1024.0*1024.0)); }

void SimpleWorldMeshRenderer::ensureProgram(){ if(program_) return; const char* vs = R"(#version 450 core
layout(location=0) in vec2 aPos; layout(location=1) in uint aPal; layout(location=2) in float aH; uniform mat4 uMVP; flat out uint vPal; out float vH; void main(){ vPal=aPal; vH=aH; gl_Position=uMVP*vec4(aPos,0,1); }
)"; const char* fs = R"(#version 450 core
layout(std140,binding=0) uniform Palette { vec4 colors[256]; }; flat in uint vPal; in float vH; out vec4 FragColor; uniform int uHeightShade; uniform vec4 uLandH; uniform vec4 uWaterH; 
vec3 shade(vec3 c, uint pal){ if(uHeightShade==0) return c; bool water=(pal==0u||pal==1u); if(water){ float mn=uWaterH.x, mx=uWaterH.y; if(mx>mn){ float t=clamp((vH-mn)/(mx-mn),0.0,1.0); c *= (1.0 - t*0.5); } } else { float mn=uLandH.x, mx=uLandH.y; if(mx>mn){ float t=clamp((vH-mn)/(mx-mn),0.0,1.0); c *= (0.6+0.4*t); } } return c; }
void main(){ vec3 base = colors[clamp(vPal,0u,255u)].rgb; base = shade(base,vPal); FragColor = vec4(base,1.0); }
)"; GLuint v=compile(GL_VERTEX_SHADER,vs); GLuint f=compile(GL_FRAGMENT_SHADER,fs); program_=link(v,f); glDeleteShader(v); glDeleteShader(f); }

void SimpleWorldMeshRenderer::render(const TileMap* map, const glm::mat4& vp, float zoom, bool force){ if(!map) return; // Rendu L2
	if(!force && zoom <= 7.5f) return; ensureProgram(); if(!vao_) buildMesh(map); if(!vao_) return; glUseProgram(program_); glUniformMatrix4fv(glGetUniformLocation(program_,"uMVP"),1,GL_FALSE,&vp[0][0]); glUniform1i(glGetUniformLocation(program_,"uHeightShade"), heightShading_?1:0); glUniform4f(glGetUniformLocation(program_,"uLandH"), map->landMinHeight, map->landMaxHeight,0,0); glUniform4f(glGetUniformLocation(program_,"uWaterH"), map->waterMinHeight, map->waterMaxHeight,0,0); glBindVertexArray(vao_); glDrawElements(GL_TRIANGLES,indexCount_,GL_UNSIGNED_INT,0); glBindVertexArray(0); }
