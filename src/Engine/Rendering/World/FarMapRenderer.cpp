#include "FarMapRenderer.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cmath>
#include <random>
#include <array>

static GLuint compile(GLenum t, const char* s){ GLuint sh=glCreateShader(t); glShaderSource(sh,1,&s,nullptr); glCompileShader(sh); return sh; }
static GLuint link(GLuint vs, GLuint fs){ GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p); return p; }

// NEW helper
float FarMapRenderer::computeNiceStep(float raw) const {
    if (raw <= 0.f) return 1.f;
    float exp10 = std::pow(10.f, std::floor(std::log10(raw)));
    float n = raw / exp10;
    float m = (n < 1.5f)?1.f: (n < 3.f)?2.f: (n < 7.f)?5.f:10.f;
    return m * exp10;
}

bool FarMapRenderer::init(const TileMap* map) {
    buildPalette(map);
    buildCountryIndexTexture(map); // uniquement pour charger les couleurs pays dans la palette
    buildRoadBuffers(map); // routes en coordonnées monde
    buildCrossesBuffer(map); // lieux en coordonnées monde
    buildAdaptiveCellsBuffer(map); // construit directement les cellules adaptatives
    // (SUPPRIME) buildPolygonBuffer / buildGridBuffer

    // Programmes minimal: palette déjà chargée; seul shader adaptatif sera créé à la volée
    return true;
}

void FarMapRenderer::shutdown() {
    if (countryIndexTex_) glDeleteTextures(1,&countryIndexTex_); // peut exister encore
    if (paletteUBO_) glDeleteBuffers(1,&paletteUBO_);
    if (roadsVbo_) glDeleteBuffers(1,&roadsVbo_);
    if (roadsIbo_) glDeleteBuffers(1,&roadsIbo_);
    if (roadsVao_) glDeleteVertexArrays(1,&roadsVao_);
    if (crossesVbo_) glDeleteBuffers(1,&crossesVbo_);
    if (crossesVao_) glDeleteVertexArrays(1,&crossesVao_);
    if (adaptiveVao_) glDeleteVertexArrays(1,&adaptiveVao_);
    if (adaptiveVbo_) glDeleteBuffers(1,&adaptiveVbo_);
    if (mapProgram_) glDeleteProgram(mapProgram_);
    if (lineProgram_) glDeleteProgram(lineProgram_);
    if (polyProgram_) glDeleteProgram(polyProgram_);
    countryIndexTex_=paletteUBO_=roadsVbo_=roadsIbo_=roadsVao_=crossesVbo_=crossesVao_=adaptiveVao_=adaptiveVbo_=mapProgram_=lineProgram_=polyProgram_=0;
}

void FarMapRenderer::rebuild(const TileMap* map) {
    buildPalette(map);
    buildCountryIndexTexture(map);
    buildRoadBuffers(map);
    buildCrossesBuffer(map);
    buildAdaptiveCellsBuffer(map);
}

void FarMapRenderer::buildPalette(const TileMap* map) {
    countryColors_.resize(256, glm::vec3(0.2f));
    // Eau
    countryColors_[0] = glm::vec3(0.0f,0.19f,0.27f);      // deep
    countryColors_[1] = glm::vec3(0.10f,0.34f,0.47f);     // shallow
    // Couleurs réalistes des biomes (index biome -> 0..N, palette = biome+2)
    auto biomeRealColor = [](size_t id)->uint32_t {
        switch(id){
            case 0:  return 0x0A3D66; // Marine (non utilisé ici normalement)
            case 1:  return 0xD9C07C; // Hot Desert
            case 2:  return 0xE3D7C9; // Cold Desert
            case 3:  return 0xC7D25A; // Savanna
            case 4:  return 0x7DB654; // Grassland
            case 5:  return 0x2F6B2F; // Tropical Seasonal Forest
            case 6:  return 0x4D7D38; // Temperate Deciduous Forest
            case 7:  return 0x0F4D2E; // Tropical Rainforest
            case 8:  return 0x1F5D46; // Temperate Rainforest
            case 9:  return 0x285138; // Taiga
            case 10: return 0x7C765A; // Tundra
            case 11: return 0xBFE8F5; // Glacier
            case 12: return 0x4B6D4D; // Wetland
            default: return 0x707070; // inconnu
        }
    };
    if (map) {
        size_t nb = map->biomeColorsRGB.size();
        for (size_t b=0; b<nb && b+2<256; ++b) {
            uint32_t rgb = map->biomeColorsRGB[b];
            // Remplacer si placeholder gris ou si on veut palette réaliste
            if (rgb == 0x707070u) rgb = biomeRealColor(b);
            else {
                // Option: forcer toujours la palette réaliste pour cohérence visuelle
                rgb = biomeRealColor(b);
            }
            float r = ((rgb>>16)&0xFF)/255.f;
            float g = ((rgb>>8 )&0xFF)/255.f;
            float bl= (rgb      &0xFF)/255.f;
            countryColors_[b+2] = glm::vec3(r,g,bl);
        }
    } else {
        for (int i=2;i<256;++i) countryColors_[i] = glm::vec3(0.4f,0.5f,0.35f);
    }
    if (!paletteUBO_) glGenBuffers(1,&paletteUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER,paletteUBO_);
    std::vector<float> packed(256*4,1.0f);
    for (int i=0;i<256;++i){ packed[i*4+0]=countryColors_[i].r; packed[i*4+1]=countryColors_[i].g; packed[i*4+2]=countryColors_[i].b; }
    glBufferData(GL_UNIFORM_BUFFER, packed.size()*sizeof(float), packed.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,paletteUBO_);
}

void FarMapRenderer::buildCountryPalette(const TileMap* map){
    if (!countryPaletteUBO_) glGenBuffers(1,&countryPaletteUBO_);
    // Construire une palette de pays (max 256) à partir de map->countryColorsRGB
    std::vector<glm::vec3> cols(256, glm::vec3(0.3f));
    if (map){
        size_t n = std::min<size_t>(map->countryColorsRGB.size(), 256);
        for (size_t i=0;i<n;++i){ uint32_t c = map->countryColorsRGB[i]; float r=((c>>16)&0xFF)/255.f; float g=((c>>8)&0xFF)/255.f; float b=(c&0xFF)/255.f; cols[i]=glm::vec3(r,g,b); }
    }
    std::vector<float> packed(256*4,1.f);
    for (int i=0;i<256;++i){ packed[i*4+0]=cols[i].r; packed[i*4+1]=cols[i].g; packed[i*4+2]=cols[i].b; }
    glBindBuffer(GL_UNIFORM_BUFFER,countryPaletteUBO_);
    glBufferData(GL_UNIFORM_BUFFER, packed.size()*sizeof(float), packed.data(), GL_DYNAMIC_DRAW);
    // Binding 1 pour pays
    glBindBufferBase(GL_UNIFORM_BUFFER,1,countryPaletteUBO_);
}

void FarMapRenderer::buildCountryIndexTexture(const TileMap* map) {
    // R16UI texture des IDs pays (0 = aucun / eau)
    if (countryIndexTex_) { glDeleteTextures(1,&countryIndexTex_); countryIndexTex_=0; }
    if (!map || map->countries.empty()) return;
    glGenTextures(1,&countryIndexTex_);
    glBindTexture(GL_TEXTURE_2D,countryIndexTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R16UI,map->width,map->height,0,GL_RED_INTEGER,GL_UNSIGNED_SHORT,map->countries.data());
    glBindTexture(GL_TEXTURE_2D,0);
    texW_=map->width; texH_=map->height;
}

void FarMapRenderer::buildRoadBuffers(const TileMap* map) {
    if (roadsVao_) { glDeleteBuffers(1,&roadsVbo_); glDeleteBuffers(1,&roadsIbo_); glDeleteVertexArrays(1,&roadsVao_); roadsVao_=roadsVbo_=roadsIbo_=0; }
    if (!map) return;
    // conversion grille -> monde
    float sx = (map->width>1 && map->worldMaxX>0)? map->worldMaxX / (float)(map->width -1) : 1.f;
    float sy = (map->height>1 && map->worldMaxY>0)? map->worldMaxY / (float)(map->height-1) : 1.f;
    std::vector<glm::vec2> verts; std::vector<unsigned int> indices; const unsigned int restart = 0xFFFFFFFFu;
    for (auto& r : map->roads) {
        if (r.points.size()<2) continue; unsigned int base = (unsigned int)verts.size();
        for (auto& p : r.points) verts.emplace_back(p.x * sx, p.y * sy);
        for (size_t i=0;i<r.points.size(); ++i) indices.push_back(base + (unsigned int)i);
        indices.push_back(restart);
    }
    if (verts.empty()) return;
    glGenVertexArrays(1,&roadsVao_);
    glGenBuffers(1,&roadsVbo_);
    glGenBuffers(1,&roadsIbo_);
    glBindVertexArray(roadsVao_);
    glBindBuffer(GL_ARRAY_BUFFER,roadsVbo_); glBufferData(GL_ARRAY_BUFFER,verts.size()*sizeof(glm::vec2),verts.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),(void*)0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,roadsIbo_); glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(unsigned int),indices.data(),GL_STATIC_DRAW);
    roadIndexCount_ = (int)indices.size();
    glBindVertexArray(0);
    // simple ligne shader si pas déjà
    if (!lineProgram_) {
        const char* lvs = R"(#version 450 core
layout(location=0) in vec2 aPos; uniform mat4 uMVP; void main(){ gl_Position=uMVP*vec4(aPos,0,1);} )";
        const char* lfs = R"(#version 450 core
out vec4 FragColor; uniform vec4 uColor; void main(){ FragColor=uColor; } )";
        GLuint lv=compile(GL_VERTEX_SHADER,lvs); GLuint lf=compile(GL_FRAGMENT_SHADER,lfs); lineProgram_=link(lv,lf); glDeleteShader(lv); glDeleteShader(lf);
    }
}

void FarMapRenderer::buildCrossesBuffer(const TileMap* map) {
    if (crossesVao_) { glDeleteBuffers(1,&crossesVbo_); glDeleteVertexArrays(1,&crossesVao_); crossesVao_=crossesVbo_=0; }
    if (!map) return;
    float sx = (map->width>1 && map->worldMaxX>0)? map->worldMaxX / (float)(map->width -1) : 1.f;
    float sy = (map->height>1 && map->worldMaxY>0)? map->worldMaxY / (float)(map->height-1) : 1.f;
    std::vector<glm::vec2> lines; lines.reserve(map->places.size()*4*2);
    for (auto& plc : map->places) {
        float x = plc.x * sx; float y = plc.y * sy;
        lines.emplace_back(x-2,y); lines.emplace_back(x+2,y);
        lines.emplace_back(x,y-2); lines.emplace_back(x,y+2);
    }
    crossVertexCount_ = (int)lines.size();
    if (lines.empty()) return;
    glGenVertexArrays(1,&crossesVao_); glGenBuffers(1,&crossesVbo_);
    glBindVertexArray(crossesVao_);
    glBindBuffer(GL_ARRAY_BUFFER,crossesVbo_); glBufferData(GL_ARRAY_BUFFER,lines.size()*sizeof(glm::vec2),lines.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),(void*)0);
    glBindVertexArray(0);
    if (!lineProgram_) {
        const char* lvs = R"(#version 450 core
layout(location=0) in vec2 aPos; uniform mat4 uMVP; void main(){ gl_Position=uMVP*vec4(aPos,0,1);} )";
        const char* lfs = R"(#version 450 core
out vec4 FragColor; uniform vec4 uColor; void main(){ FragColor=uColor; } )";
        GLuint lv=compile(GL_VERTEX_SHADER,lvs); GLuint lf=compile(GL_FRAGMENT_SHADER,lfs); lineProgram_=link(lv,lf); glDeleteShader(lv); glDeleteShader(lf);
    }
}

void FarMapRenderer::buildPolygonBuffer(const TileMap* /*map*/) { /* désactivé */ }
void FarMapRenderer::buildGridBuffer(const TileMap* /*map*/) { /* désactivé */ }
void FarMapRenderer::rebuildDynamicGrid(const TileMap* /*map*/, float /*zoom*/) { /* désactivé */ }

void FarMapRenderer::buildAdaptiveCellsBuffer(const TileMap* map) {
    if (adaptiveVao_) { glDeleteBuffers(1,&adaptiveVbo_); glDeleteVertexArrays(1,&adaptiveVao_); adaptiveVao_=adaptiveVbo_=0; adaptiveInstanceCount_=0; }
    if (!map || map->adaptiveCells.empty()) return;
    struct Inst { float x,y,w,h; unsigned short idx; unsigned short pad; };
    std::vector<Inst> inst; inst.reserve(map->adaptiveCells.size());
    for (auto &c : map->adaptiveCells) inst.push_back({c.x,c.y,c.w,c.h,c.paletteIndex,0});
    glGenVertexArrays(1,&adaptiveVao_); glBindVertexArray(adaptiveVao_);
    glGenBuffers(1,&adaptiveVbo_); glBindBuffer(GL_ARRAY_BUFFER, adaptiveVbo_);
    glBufferData(GL_ARRAY_BUFFER, inst.size()*sizeof(Inst), inst.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Inst),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribIPointer(1,1,GL_UNSIGNED_SHORT,sizeof(Inst),(void*)(4*sizeof(float)));
    glVertexAttribDivisor(0,1); glVertexAttribDivisor(1,1);
    glBindVertexArray(0);
    adaptiveInstanceCount_ = (int)inst.size();
}

void FarMapRenderer::render(const TileMap* map, const glm::mat4& vp, float zoom) {
    static constexpr float kFarMapMaxZoom = 7.5f;
    if (!map) return; if (zoom > kFarMapMaxZoom) return;
    if (adaptiveVao_==0 && map && !map->adaptiveCells.empty()) buildAdaptiveCellsBuffer(map);

    // Construire palettes pays si besoin
    if (!countryPaletteUBO_) buildCountryPalette(map);
    if (!countryIndexTex_) buildCountryIndexTexture(map);

    static GLuint adaptProg2 = 0; static bool shaderBuilt=false;
    if(!shaderBuilt){
        const char* vs = R"(#version 450 core
layout(location=0) in vec4 iRect; layout(location=1) in uint iIdx; layout(location=2) in float iMean; uniform mat4 uMVP; flat out uint vIdx; out vec2 vUV; out float vH; out vec2 vWorld; flat out vec4 vRect; 
vec2 corner(int vid){int p=vid%6; if(p==0)return vec2(0,0); if(p==1)return vec2(1,0); if(p==2)return vec2(1,1); if(p==3)return vec2(0,0); if(p==4)return vec2(1,1); return vec2(0,1);} 
void main(){ vec2 c=corner(gl_VertexID); vUV=c; vIdx=iIdx; vH=iMean; vRect=iRect; vWorld=iRect.xy + c*iRect.zw; gl_Position=uMVP*vec4(vWorld,0,1);} )";
        const char* fs = R"(#version 450 core
layout(std140,binding=0) uniform Palette { vec4 colors[256]; }; // biomes
layout(std140,binding=1) uniform CountryPalette { vec4 cColors[256]; }; // pays
layout(std140,binding=2) uniform NeutralMask { uvec4 mask[2]; }; // 256 bits (8x32 -> we pack in two uvec4)
flat in uint vIdx; in vec2 vUV; in float vH; in vec2 vWorld; flat in vec4 vRect; out vec4 FragColor;
uniform int uShowBorders; uniform vec4 uLandH; uniform vec4 uWaterH; uniform int uHeightShade; uniform int uShowCountries; uniform float uCountryAlpha; uniform ivec2 uGridSize; uniform vec2 uWorldSize; uniform usampler2D uCountryTex;
vec3 applyHeight(vec3 base, uint idx, float h){ if(uHeightShade==0) return base; if(idx==0u||idx==1u){ float mn=uWaterH.z, mx=uWaterH.w; if(mx>mn){ float t=clamp((h-mn)/(mx-mn),0.0,1.0); base *= (1.0 - t*0.6); } } else { float mn=uLandH.x, mx=uLandH.y; if(mx>mn){ float t=clamp((h-mn)/(mx-mn),0.0,1.0); base *= (0.6+0.4*t); } } return base; }
vec4 biomeColor(uint idx){ return colors[clamp(idx,0u,255u)]; }
uint countryIdAt(vec2 world){ if(uWorldSize.x<=0||uWorldSize.y<=0) return 0u; float gx = world.x / uWorldSize.x * float(uGridSize.x-1); float gy = world.y / uWorldSize.y * float(uGridSize.y-1); ivec2 ig = ivec2(clamp(vec2(round(gx),round(gy)), vec2(0.0), vec2(float(uGridSize.x-1), float(uGridSize.y-1)))); return texelFetch(uCountryTex, ig, 0).r; }
vec3 countryColor(uint id){ return cColors[clamp(id,0u,255u)].rgb; }
bool isNeutral(uint id){ if(id==0u) return true; uint word=id/32u; uint bit=id%32u; if(word>=8u) return false; uint idx=word/4u; uint sub=word%4u; uint v = mask[idx][sub]; return ((v>>bit)&1u)==1u; }
void main(){ vec4 base = biomeColor(vIdx); base.rgb = applyHeight(base.rgb, vIdx, vH); bool isWater=(vIdx==0u||vIdx==1u); if (uShowBorders==1 && !isWater){ float d=min(min(vUV.x,vUV.y),min(1.0-vUV.x,1.0-vUV.y)); float px=(fwidth(vUV.x)+fwidth(vUV.y))*0.5; float edge=smoothstep(0.0,px,d); vec3 borderColor=base.rgb*0.4; base.rgb=mix(borderColor,base.rgb,0.4+0.6*edge);} if(uShowCountries==1){ uint cid = countryIdAt(vWorld); if(cid>0u && !isNeutral(cid)){ vec3 cc = countryColor(cid); base.rgb = mix(base.rgb, cc, uCountryAlpha); } } FragColor=base; } )";
        GLuint v=compile(GL_VERTEX_SHADER,vs); GLuint f=compile(GL_FRAGMENT_SHADER,fs); adaptProg2=link(v,f); glDeleteShader(v); glDeleteShader(f); shaderBuilt=true;
    }
    // Upload neutral mask if dirty
    if(neutralMaskDirty_){
        if(!neutralMaskUBO_) glGenBuffers(1,&neutralMaskUBO_);
        glBindBuffer(GL_UNIFORM_BUFFER, neutralMaskUBO_);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(neutralMask_), neutralMask_.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER,2, neutralMaskUBO_);
        neutralMaskDirty_=false;
    }
    // Update palette with biome colors if map provided
    if(map){
        // paletteUBO déjà contient les bonnes couleurs; rien à refaire sauf si biomes > 254
    }
    // Rebuild adaptive buffer with meanHeight attribute if not yet (add new attribute)
    if (adaptiveVao_ && map && !map->adaptiveCells.empty()) {
        // Recreate buffer with extended Inst struct if missing attribute 2
        glDeleteBuffers(1,&adaptiveVbo_); glDeleteVertexArrays(1,&adaptiveVao_); adaptiveVbo_=adaptiveVao_=0; adaptiveInstanceCount_=0;
        struct Inst { float x,y,w,h; float mean; unsigned short idx; unsigned short pad; };
        std::vector<Inst> inst; inst.reserve(map->adaptiveCells.size());
        for(auto &c: map->adaptiveCells){ inst.push_back({c.x,c.y,c.w,c.h,c.meanHeight,c.paletteIndex,0}); }
        glGenVertexArrays(1,&adaptiveVao_); glBindVertexArray(adaptiveVao_);
        glGenBuffers(1,&adaptiveVbo_); glBindBuffer(GL_ARRAY_BUFFER, adaptiveVbo_);
        glBufferData(GL_ARRAY_BUFFER, inst.size()*sizeof(Inst), inst.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Inst),(void*)0);
        glEnableVertexAttribArray(1); glVertexAttribIPointer(1,1,GL_UNSIGNED_SHORT,sizeof(Inst),(void*)(5*sizeof(float)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,sizeof(Inst),(void*)(4*sizeof(float)));
        glVertexAttribDivisor(0,1); glVertexAttribDivisor(1,1); glVertexAttribDivisor(2,1);
        glBindVertexArray(0); adaptiveInstanceCount_=(int)inst.size();
    }
    if (adaptiveVao_ && adaptiveInstanceCount_>0) {
        glUseProgram(adaptProg2);
        glUniformMatrix4fv(glGetUniformLocation(adaptProg2,"uMVP"),1,GL_FALSE,&vp[0][0]);
        glUniform1i(glGetUniformLocation(adaptProg2,"uShowBorders"), showAdaptive_?1:0);
        glUniform4f(glGetUniformLocation(adaptProg2,"uLandH"),map->landMinHeight,map->landMaxHeight,0,0);
        glUniform4f(glGetUniformLocation(adaptProg2,"uWaterH"),0,0,map->waterMinHeight,map->waterMaxHeight);
        glUniform1i(glGetUniformLocation(adaptProg2,"uHeightShade"), heightShading_?1:0);
        glUniform1i(glGetUniformLocation(adaptProg2,"uShowCountries"), showCountries_?1:0);
        glUniform1f(glGetUniformLocation(adaptProg2,"uCountryAlpha"), countryAlpha_);
        glUniform2i(glGetUniformLocation(adaptProg2,"uGridSize"), map->width, map->height);
        glUniform2f(glGetUniformLocation(adaptProg2,"uWorldSize"), map->worldMaxX, map->worldMaxY);
        if (countryIndexTex_) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,countryIndexTex_); glUniform1i(glGetUniformLocation(adaptProg2,"uCountryTex"),0); }
        glBindVertexArray(adaptiveVao_); glDrawArraysInstanced(GL_TRIANGLES,0,6,adaptiveInstanceCount_); glBindVertexArray(0);
    }

    // Routes
    if (roadsVao_ && roadIndexCount_>0) {
        glEnable(GL_PRIMITIVE_RESTART); glPrimitiveRestartIndex(0xFFFFFFFFu);
        glUseProgram(lineProgram_);
        GLint lMvp = glGetUniformLocation(lineProgram_,"uMVP"); glUniformMatrix4fv(lMvp,1,GL_FALSE,&vp[0][0]);
        GLint col = glGetUniformLocation(lineProgram_,"uColor"); glUniform4f(col,1,1,0,1);
        glBindVertexArray(roadsVao_); glDrawElements(GL_LINE_STRIP, roadIndexCount_, GL_UNSIGNED_INT, 0); glBindVertexArray(0);
        glDisable(GL_PRIMITIVE_RESTART);
    }
    // Lieux
    if (crossesVao_ && crossVertexCount_>0) {
        glUseProgram(lineProgram_);
        GLint lMvp = glGetUniformLocation(lineProgram_,"uMVP"); glUniformMatrix4fv(lMvp,1,GL_FALSE,&vp[0][0]);
        GLint col = glGetUniformLocation(lineProgram_,"uColor"); glUniform4f(col,1,0,0,1);
        glBindVertexArray(crossesVao_); glDrawArrays(GL_LINES,0,crossVertexCount_); glBindVertexArray(0);
    }
}

void FarMapRenderer::setCountryNeutral(uint16_t id, bool neutral){
    if (id>=256) return; uint16_t word=id/32, bit=id%32; uint32_t mask=(1u<<bit); if(neutral) neutralMask_[word]|=mask; else neutralMask_[word]&=~mask; neutralMaskDirty_=true; }
bool FarMapRenderer::isCountryNeutral(uint16_t id) const { if(id>=256) return false; uint16_t word=id/32, bit=id%32; return (neutralMask_[word]>>bit)&1u; }
