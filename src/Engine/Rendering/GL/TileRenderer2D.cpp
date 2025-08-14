#include "TileRenderer2D.h"
#include "TileAtlas.h"
#include "TileMap.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

static unsigned int Compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

static unsigned int Link(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    return p;
}

bool TileRenderer2D::init() {
    const char* vs = R"(#version 450 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; uniform mat4 uMVP; out vec2 vUV; void main(){ vUV=aUV; gl_Position=uMVP*vec4(aPos,0,1);} )";
    const char* fs = R"(#version 450 core
layout(binding=0) uniform sampler2D uTex; in vec2 vUV; out vec4 FragColor; void main(){ FragColor = texture(uTex, vUV);} )";
    GLuint vsh = Compile(GL_VERTEX_SHADER, vs);
    GLuint fsh = Compile(GL_FRAGMENT_SHADER, fs);
    program_ = Link(vsh, fsh);
    glDeleteShader(vsh); glDeleteShader(fsh);

    // Grid shader
    const char* gvs = R"(#version 450 core
layout(location=0) in vec2 aPos; uniform mat4 uMVP; void main(){ gl_Position=uMVP*vec4(aPos,0,1);} )";
    const char* gfs = R"(#version 450 core
out vec4 FragColor; uniform vec2 uCell; uniform vec4 uColors; // x: line, y: major
uniform float uMajorEvery;
void main(){
    vec2 p = gl_FragCoord.xy / uCell; // approximate
    float gx = fract(p.x); float gy = fract(p.y);
    float line = step(gx,0.01)+step(gy,0.01);
    float major = (mod(floor(p.x), uMajorEvery)==0 || mod(floor(p.y), uMajorEvery)==0) ? 1.0:0.0;
    vec3 base = mix(vec3(uColors.a), vec3(uColors.rgb), major);
    float intensity = clamp(line,0.0,1.0);
    FragColor = vec4(base*intensity, intensity);
} )";
    GLuint gv = Compile(GL_VERTEX_SHADER, gvs);
    GLuint gf = Compile(GL_FRAGMENT_SHADER, gfs);
    gridProgram_ = Link(gv, gf);
    glDeleteShader(gv); glDeleteShader(gf);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);
    return true;
}

void TileRenderer2D::shutdown() {
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    if (gridProgram_) glDeleteProgram(gridProgram_);
    ibo_=vbo_=vao_=program_=gridProgram_=0;
}

void TileRenderer2D::render(const TileMap& map, const TileAtlas& atlas, const glm::mat4& vp) {
    if (!program_) return;
    glUseProgram(program_);
    glBindVertexArray(vao_);
    struct V { float x,y,u,v; };
    V verts[4] = {{-1,-1,0,0},{1,-1,1,0},{1,1,1,1},{-1,1,0,1}};
    unsigned short idx[6]={0,1,2,0,2,3};
    glBindBuffer(GL_ARRAY_BUFFER, vbo_); glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(V),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(V),(void*)(2*sizeof(float)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.textureId());
    GLint loc = glGetUniformLocation(program_,"uMVP"); glUniformMatrix4fv(loc,1,GL_FALSE,&vp[0][0]);
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0);
    glBindVertexArray(0);
}

void TileRenderer2D::renderGrid(int cellsX, int cellsY, float cellSize, const glm::mat4& vp) {
    if (!gridProgram_) return;
    // Full quad covering view; shader computes lines
    struct V { float x,y; };
    V verts[4] = {{0,0},{cellsX*cellSize,0},{cellsX*cellSize,cellsY*cellSize},{0,cellsY*cellSize}};
    unsigned short idx[6]={0,1,2,0,2,3};
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER,vbo_); glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo_); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(V),(void*)0);

    glUseProgram(gridProgram_);
    GLint mvpLoc = glGetUniformLocation(gridProgram_,"uMVP");
    glUniformMatrix4fv(mvpLoc,1,GL_FALSE,&vp[0][0]);
    GLint cellLoc = glGetUniformLocation(gridProgram_,"uCell"); glUniform2f(cellLoc,cellSize,cellSize);
    GLint colLoc = glGetUniformLocation(gridProgram_,"uColors"); glUniform4f(colLoc, 0.9f,0.9f,0.9f,0.15f);
    GLint majLoc = glGetUniformLocation(gridProgram_,"uMajorEvery"); glUniform1f(majLoc, 10.0f);
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0);
    glBindVertexArray(0);
}
