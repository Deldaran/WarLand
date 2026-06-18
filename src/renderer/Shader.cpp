#include "renderer/Shader.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <iostream>
#include <utility>

namespace wl {

namespace {
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Shader] Impossible d'ouvrir: " << path << "\n";
        return {};
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
} // namespace

Shader::~Shader() {
    if (m_id) {
        glDeleteProgram(m_id);
    }
}

Shader::Shader(Shader&& other) noexcept : m_id(other.m_id) {
    other.m_id = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_id) glDeleteProgram(m_id);
        m_id = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

uint32_t Shader::compileStage(uint32_t type, const std::string& source, const std::string& label) {
    uint32_t shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[Shader] Erreur de compilation (" << label << "):\n" << log << "\n";
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vsrc = readFile(vertexPath);
    std::string fsrc = readFile(fragmentPath);
    if (vsrc.empty() || fsrc.empty()) {
        return false;
    }

    uint32_t vs = compileStage(GL_VERTEX_SHADER, vsrc, vertexPath);
    uint32_t fs = compileStage(GL_FRAGMENT_SHADER, fsrc, fragmentPath);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    uint32_t program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[Shader] Erreur de link:\n" << log << "\n";
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!program) return false;

    if (m_id) glDeleteProgram(m_id);
    m_id = program;
    return true;
}

bool Shader::loadWithTess(const std::string& vertexPath, const std::string& tescPath,
                          const std::string& tesePath, const std::string& fragmentPath) {
    std::string vsrc = readFile(vertexPath);
    std::string csrc = readFile(tescPath);
    std::string esrc = readFile(tesePath);
    std::string fsrc = readFile(fragmentPath);
    if (vsrc.empty() || csrc.empty() || esrc.empty() || fsrc.empty()) return false;

    uint32_t vs = compileStage(GL_VERTEX_SHADER, vsrc, vertexPath);
    uint32_t cs = compileStage(GL_TESS_CONTROL_SHADER, csrc, tescPath);
    uint32_t es = compileStage(GL_TESS_EVALUATION_SHADER, esrc, tesePath);
    uint32_t fs = compileStage(GL_FRAGMENT_SHADER, fsrc, fragmentPath);
    if (!vs || !cs || !es || !fs) {
        if (vs) glDeleteShader(vs); if (cs) glDeleteShader(cs);
        if (es) glDeleteShader(es); if (fs) glDeleteShader(fs);
        return false;
    }

    uint32_t program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, cs);
    glAttachShader(program, es);
    glAttachShader(program, fs);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[Shader] Erreur de link (tess):\n" << log << "\n";
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vs); glDeleteShader(cs); glDeleteShader(es); glDeleteShader(fs);
    if (!program) return false;
    if (m_id) glDeleteProgram(m_id);
    m_id = program;
    return true;
}

void Shader::bind() const { glUseProgram(m_id); }
void Shader::unbind() const { glUseProgram(0); }

int Shader::uniformLocation(const std::string& name) const {
    return glGetUniformLocation(m_id, name.c_str());
}

void Shader::setMat4(const std::string& name, const glm::mat4& value) const {
    glUniformMatrix4fv(uniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setVec2(const std::string& name, const glm::vec2& value) const {
    glUniform2fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
    glUniform3fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setFloat(const std::string& name, float value) const {
    glUniform1f(uniformLocation(name), value);
}

void Shader::setInt(const std::string& name, int value) const {
    glUniform1i(uniformLocation(name), value);
}

} // namespace wl
