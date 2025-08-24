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

bool SimpleWorldMeshRenderer::init(const TileMap* map){ buildMesh(map); ensureProgram(); return true; }
void SimpleWorldMeshRenderer::shutdown(){
	if(ibo_) glDeleteBuffers(1,&ibo_); if(vbo_) glDeleteBuffers(1,&vbo_); if(vao_) glDeleteVertexArrays(1,&vao_);
	if(program_) glDeleteProgram(program_);
	if(iboT_) glDeleteBuffers(1,&iboT_); if(vboT_) glDeleteBuffers(1,&vboT_); if(vaoT_) glDeleteVertexArrays(1,&vaoT_);
	if(programTess_) glDeleteProgram(programTess_);
	if(heightTex_) glDeleteTextures(1,&heightTex_);
	vao_=vbo_=ibo_=program_=0; indexCount_=0; vaoT_=vboT_=iboT_=0; indexCountT_=0; programTess_=0; heightTex_=0; hmW_=hmH_=0;
}
void SimpleWorldMeshRenderer::rebuild(const TileMap* map){ if(adaptiveEnabled_) buildAdaptiveMesh(map); else buildMesh(map); }

void SimpleWorldMeshRenderer::buildMesh(const TileMap* map){ if(vao_){ glDeleteBuffers(1,&vbo_); glDeleteBuffers(1,&ibo_); glDeleteVertexArrays(1,&vao_); vao_=vbo_=ibo_=0; indexCount_=0; }
	if(!map||map->width<=1||map->height<=1) return; builtWidth_=map->width; builtHeight_=map->height;
	// Générer un grid mesh triangle strip par ligne (avec indices dégénérés) ou simple éléments.
	// Pour simplicité initiale: indices triangles explicites (2 tris par cellule) -> 6 indices * (w-1)*(h-1)
	const int w=map->width; const int h=map->height; const float sx = (w>1 && map->worldMaxX>0)? map->worldMaxX/(float)(w-1):1.f; const float sy = (h>1 && map->worldMaxY>0)? map->worldMaxY/(float)(h-1):1.f;
	struct V { float x,y; float height; }; std::vector<V> verts; verts.resize((size_t)w*h);
	for(int y=0;y<h;++y){ for(int x=0;x<w;++x){ size_t idx=(size_t)y*w+x; float ht = (idx < map->tileHeights.size())? map->tileHeights[idx]:0.f; verts[idx] = { x*sx, y*sy, ht }; }}
	std::vector<uint32_t> idxs; idxs.reserve((size_t)(w-1)*(h-1)*6);
	for(int y=0;y<h-1;++y){ for(int x=0;x<w-1;++x){ uint32_t i0=y*w+x; uint32_t i1=y*w+x+1; uint32_t i2=(y+1)*w+x; uint32_t i3=(y+1)*w+x+1; // two triangles i0,i2,i1 and i1,i2,i3
			idxs.push_back(i0); idxs.push_back(i2); idxs.push_back(i1); idxs.push_back(i1); idxs.push_back(i2); idxs.push_back(i3); }}
	glGenVertexArrays(1,&vao_); glBindVertexArray(vao_); glGenBuffers(1,&vbo_); glBindBuffer(GL_ARRAY_BUFFER,vbo_); glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(V), verts.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(V),(void*)0);
	glEnableVertexAttribArray(1); glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,sizeof(V),(void*)(2*sizeof(float)));
	glGenBuffers(1,&ibo_); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo_); glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxs.size()*sizeof(uint32_t), idxs.data(), GL_STATIC_DRAW); indexCount_=(int)idxs.size(); glBindVertexArray(0);
	std::fprintf(stderr,"[L2Mesh] Built mesh %dx%d cells (%zu verts, %d indices) ~%.2f MB\n", w,h, verts.size(), indexCount_, (verts.size()*sizeof(V)+idxs.size()*sizeof(uint32_t))/ (1024.0*1024.0)); }

void SimpleWorldMeshRenderer::buildAdaptiveMesh(const TileMap* map){ if(vao_){ glDeleteBuffers(1,&vbo_); glDeleteBuffers(1,&ibo_); glDeleteVertexArrays(1,&vao_); vao_=vbo_=ibo_=0; indexCount_=0; }
	if(!map||map->width<=1||map->height<=1) return; builtWidth_=map->width; builtHeight_=map->height;
	const int w=map->width, h=map->height; const float sx = (w>1 && map->worldMaxX>0)? map->worldMaxX/(float)(w-1):1.f; const float sy = (h>1 && map->worldMaxY>0)? map->worldMaxY/(float)(h-1):1.f;
	// Build a mixed-resolution grid: fine step=1 inside radius, coarser step outside
	const float r2 = adaptiveRadius_ * adaptiveRadius_;
	const int fineStep = 1;
	const int coarseStep = std::max(outerStep_, 2);
	struct V { float x,y; float height; };
	std::vector<V> verts; std::vector<uint32_t> idxs;
	// Helper to push a quad (two tris) indices on a regular row-major lattice
	auto pushQuad = [&](int gx, int gy, int step, const std::vector<int>& rowStarts, int gridW){
		int x0 = gx, y0 = gy; int x1 = std::min(gx+step, w-1); int y1 = std::min(gy+step, h-1);
		int i00 = rowStarts[y0] + x0; int i10 = rowStarts[y0] + x1; int i01 = rowStarts[y1] + x0; int i11 = rowStarts[y1] + x1;
		idxs.push_back(i00); idxs.push_back(i01); idxs.push_back(i10);
		idxs.push_back(i10); idxs.push_back(i01); idxs.push_back(i11);
	};
	// Build vertex grid with variable row starts
	std::vector<int> rowStarts(h, 0); int vertCount=0;
	for(int y=0;y<h;++y){ rowStarts[y]=vertCount; int step = coarseStep; for(int x=0;x<w;x+=step){ float wx=x*sx, wy=y*sy; size_t idx=(size_t)y*w+x; float ht=(idx<map->tileHeights.size())? map->tileHeights[idx]:0.f; verts.push_back({wx,wy,ht}); ++vertCount; }
		// refine inside radius by inserting missing fine vertices
		for(int x=0;x<w;++x){ float wx=x*sx, wy=y*sy; float dx=wx-camX_, dy=wy-camY_; if((dx*dx+dy*dy)<=r2){ // ensure fine sampling at step=1
				// if this x wasn't added due to coarse step, add it now
				size_t idx=(size_t)y*w+x; float ht=(idx<map->tileHeights.size())? map->tileHeights[idx]:0.f; verts.push_back({wx,wy,ht}); ++vertCount; }
		}
	}
	// For simplicity, connect quads at fine step within radius and coarse elsewhere
	for(int y=0;y<h-1;++y){ for(int x=0;x<w-1;++x){ float wx=x*sx, wy=y*sy; float dx=wx-camX_, dy=wy-camY_; int step = ((dx*dx+dy*dy)<=r2)? fineStep : coarseStep; pushQuad(x,y,step,rowStarts,w); }}
	glGenVertexArrays(1,&vao_); glBindVertexArray(vao_);
	glGenBuffers(1,&vbo_); glBindBuffer(GL_ARRAY_BUFFER,vbo_); glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(V), verts.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(V),(void*)0);
	glEnableVertexAttribArray(1); glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,sizeof(V),(void*)(2*sizeof(float)));
	glGenBuffers(1,&ibo_); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo_); glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxs.size()*sizeof(uint32_t), idxs.data(), GL_STATIC_DRAW); indexCount_=(int)idxs.size(); glBindVertexArray(0);
	std::fprintf(stderr,"[L2Mesh] Built adaptive mesh (r=%.1f) verts=%zu indices=%d\n", adaptiveRadius_, verts.size(), indexCount_);
}

void SimpleWorldMeshRenderer::ensureProgram(){ if(program_) return; const char* vs = R"(#version 450 core
layout(location=0) in vec2 aPos; layout(location=1) in float aH; uniform mat4 uMVP; uniform float uHeightScale; uniform vec2 uLandH; out float vH; 
void main(){ vH=aH; float baseMin = uLandH.x; float z = (aH - baseMin) * uHeightScale; vec3 pos = vec3(aPos.xy, z); gl_Position=uMVP*vec4(pos,1); }
)"; const char* fs = R"(#version 450 core
in float vH; out vec4 FragColor; uniform int uHeightShade; uniform vec2 uLandH; 
void main(){ float t=0.0; if(uHeightShade!=0){ float mn=uLandH.x, mx=uLandH.y; if(mx>mn) t=clamp((vH-mn)/(mx-mn),0.0,1.0);} vec3 base = mix(vec3(0.55,0.55,0.55), vec3(0.95,0.95,0.95), t); FragColor = vec4(base,1.0); }
)"; GLuint v=compile(GL_VERTEX_SHADER,vs); GLuint f=compile(GL_FRAGMENT_SHADER,fs); program_=link(v,f); glDeleteShader(v); glDeleteShader(f); }

void SimpleWorldMeshRenderer::ensureProgramTess(){ if(programTess_) return; const char* vs = R"(#version 450 core
layout(location=0) in vec2 aPos; void main(){ gl_Position = vec4(aPos, 0.0, 1.0); }
)"; const char* tcs = R"(#version 450 core
layout(vertices=4) out; uniform vec2 uCam; uniform float uTessNear; uniform float uTessFar; uniform int uTessMin; uniform int uTessMax;
void main(){ gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position; barrier();
	vec2 c = 0.25*(gl_in[0].gl_Position.xy + gl_in[1].gl_Position.xy + gl_in[2].gl_Position.xy + gl_in[3].gl_Position.xy);
	float d = distance(c, uCam);
	float t = clamp((d - uTessNear) / max(uTessFar - uTessNear, 1e-4), 0.0, 1.0);
	float level = mix(float(uTessMax), float(uTessMin), t);
	gl_TessLevelOuter[0] = level; gl_TessLevelOuter[1] = level; gl_TessLevelOuter[2] = level; gl_TessLevelOuter[3] = level;
	gl_TessLevelInner[0] = level; gl_TessLevelInner[1] = level; }
)"; const char* tes = R"(#version 450 core
layout(quads, fractional_even_spacing, ccw) in; uniform mat4 uMVP; uniform sampler2D uHeightTex; uniform vec2 uWorldSize; uniform vec2 uLandH; uniform float uHeightScale; out float vH;
vec2 bilerp(vec2 a, vec2 b, vec2 c, vec2 d, vec2 uv){ vec2 ab = mix(a, b, uv.x); vec2 cd = mix(c, d, uv.x); return mix(ab, cd, uv.y); }
void main(){ vec2 p0 = gl_in[0].gl_Position.xy; vec2 p1 = gl_in[1].gl_Position.xy; vec2 p2 = gl_in[2].gl_Position.xy; vec2 p3 = gl_in[3].gl_Position.xy; vec2 uv = gl_TessCoord.xy; vec2 p = bilerp(p0,p1,p2,p3, uv); vec2 tex = vec2(p.x / max(uWorldSize.x,1e-6), p.y / max(uWorldSize.y,1e-6)); float h = texture(uHeightTex, tex).r; vH = h; float z = max(0.0, (h - uLandH.x)) * uHeightScale; gl_Position = uMVP * vec4(p, z, 1.0); }
)"; const char* fs = R"(#version 450 core
in float vH; out vec4 FragColor; uniform int uHeightShade; uniform vec2 uLandH; void main(){ float t=0.0; if(uHeightShade!=0){ float mn=uLandH.x, mx=uLandH.y; if(mx>mn) t=clamp((vH-mn)/(mx-mn),0.0,1.0);} vec3 base = mix(vec3(0.55,0.55,0.55), vec3(0.95,0.95,0.95), t); FragColor = vec4(base,1.0);} 
)"; GLuint sv=compile(GL_VERTEX_SHADER,vs); GLuint sc=compile(GL_TESS_CONTROL_SHADER,tcs); GLuint se=compile(GL_TESS_EVALUATION_SHADER,tes); GLuint sf=compile(GL_FRAGMENT_SHADER,fs); programTess_=glCreateProgram(); glAttachShader(programTess_,sv); glAttachShader(programTess_,sc); glAttachShader(programTess_,se); glAttachShader(programTess_,sf); glLinkProgram(programTess_); GLint ok=0; glGetProgramiv(programTess_,GL_LINK_STATUS,&ok); if(!ok){ char log[4096]; glGetProgramInfoLog(programTess_,4096,nullptr,log); std::fprintf(stderr,"[L2Mesh][Tess] Link error: %s\n", log);} glDeleteShader(sv); glDeleteShader(sc); glDeleteShader(se); glDeleteShader(sf); }

void SimpleWorldMeshRenderer::uploadHeightTex(const TileMap* map){
	if(!map) return;
	const void* data = map->tileHeights.empty()? nullptr : (const void*)map->tileHeights.data();
	if(!heightTex_ || hmW_!=map->width || hmH_!=map->height){
		if(heightTex_) { glDeleteTextures(1,&heightTex_); heightTex_=0; }
		hmW_ = map->width; hmH_ = map->height; glGenTextures(1,&heightTex_); glBindTexture(GL_TEXTURE_2D,heightTex_);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,hmW_,hmH_,0,GL_RED,GL_FLOAT,data);
	} else {
		glBindTexture(GL_TEXTURE_2D,heightTex_);
		glTexSubImage2D(GL_TEXTURE_2D,0,0,0,hmW_,hmH_,GL_RED,GL_FLOAT,data);
	}
	glGenerateMipmap(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,0);
}

void SimpleWorldMeshRenderer::buildTessGrid(const TileMap* map){ if(vaoT_){ glDeleteBuffers(1,&vboT_); glDeleteBuffers(1,&iboT_); glDeleteVertexArrays(1,&vaoT_); vaoT_=vboT_=iboT_=0; indexCountT_=0; }
	if(!map||map->width<=1||map->height<=1) return; const int w=map->width, h=map->height; const float sx = (w>1 && map->worldMaxX>0)? map->worldMaxX/(float)(w-1):1.f; const float sy = (h>1 && map->worldMaxY>0)? map->worldMaxY/(float)(h-1):1.f;
	struct V { float x,y; }; std::vector<V> verts; std::vector<uint32_t> idxs; int step = std::max(tessBaseStep_, 2);
	// Build coarse grid vertices
	for(int y=0;y<h; y+=step){ for(int x=0;x<w; x+=step){ verts.push_back({ x*sx, y*sy }); }}
	auto vertIndex = [&](int gx, int gy){ int ix = gx/step; int iy = gy/step; int cols = (w + step - 1)/step; return (uint32_t)(iy * cols + ix); };
	int cols = (w + step - 1)/step; int rows = (h + step - 1)/step;
	for(int gy=0; gy<rows-1; ++gy){ for(int gx=0; gx<cols-1; ++gx){ uint32_t i00 = (uint32_t)(gy*cols+gx); uint32_t i10 = i00+1; uint32_t i01 = (uint32_t)((gy+1)*cols+gx); uint32_t i11 = i01+1; idxs.push_back(i00); idxs.push_back(i10); idxs.push_back(i01); idxs.push_back(i11); }}
	glGenVertexArrays(1,&vaoT_); glBindVertexArray(vaoT_); glGenBuffers(1,&vboT_); glBindBuffer(GL_ARRAY_BUFFER,vboT_); glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(V), verts.data(), GL_STATIC_DRAW); glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(V),(void*)0); glGenBuffers(1,&iboT_); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,iboT_); glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxs.size()*sizeof(uint32_t), idxs.data(), GL_STATIC_DRAW); indexCountT_=(int)idxs.size(); glBindVertexArray(0);
}

void SimpleWorldMeshRenderer::render(const TileMap* map, const glm::mat4& vp, float zoom, bool force){ if(!map) return; // Rendu L2
	if(!force && zoom <= 7.5f) return;
	if(useTess_) {
		ensureProgramTess(); uploadHeightTex(map); if(!vaoT_) buildTessGrid(map); if(!vaoT_) return;
		glUseProgram(programTess_);
		glUniformMatrix4fv(glGetUniformLocation(programTess_,"uMVP"),1,GL_FALSE,&vp[0][0]);
		glUniform1f(glGetUniformLocation(programTess_,"uHeightScale"), heightScale_);
		glUniform2f(glGetUniformLocation(programTess_,"uLandH"), map->landMinHeight, map->landMaxHeight);
		glUniform2f(glGetUniformLocation(programTess_,"uWorldSize"), map->worldMaxX>0? map->worldMaxX:(float)map->width, map->worldMaxY>0? map->worldMaxY:(float)map->height);
		glUniform1i(glGetUniformLocation(programTess_,"uHeightShade"), heightShading_?1:0);
		glUniform2f(glGetUniformLocation(programTess_,"uCam"), camX_, camY_);
		glUniform1f(glGetUniformLocation(programTess_,"uTessNear"), tessNear_);
		glUniform1f(glGetUniformLocation(programTess_,"uTessFar"), tessFar_);
		glUniform1i(glGetUniformLocation(programTess_,"uTessMin"), tessMin_);
		glUniform1i(glGetUniformLocation(programTess_,"uTessMax"), tessMax_);
		glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,heightTex_); glUniform1i(glGetUniformLocation(programTess_,"uHeightTex"), 0);
		glBindVertexArray(vaoT_); glPatchParameteri(GL_PATCH_VERTICES,4); glDrawElements(GL_PATCHES,indexCountT_,GL_UNSIGNED_INT,0); glBindVertexArray(0); glBindTexture(GL_TEXTURE_2D,0);
		return;
	}
	ensureProgram();
	// Rebuild adaptively if enabled and no buffers yet
	if(!vao_ || needsRebuild_) { if(adaptiveEnabled_) buildAdaptiveMesh(map); else buildMesh(map); needsRebuild_=false; }
	if(!vao_) return; glUseProgram(program_); glUniformMatrix4fv(glGetUniformLocation(program_,"uMVP"),1,GL_FALSE,&vp[0][0]); glUniform1f(glGetUniformLocation(program_,"uHeightScale"), heightScale_); glUniform2f(glGetUniformLocation(program_,"uLandH"), map->landMinHeight, map->landMaxHeight); glUniform1i(glGetUniformLocation(program_,"uHeightShade"), heightShading_?1:0); glBindVertexArray(vao_); glDrawElements(GL_TRIANGLES,indexCount_,GL_UNSIGNED_INT,0); glBindVertexArray(0); }
