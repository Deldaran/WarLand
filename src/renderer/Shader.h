#pragma once

#include <string>
#include <cstdint>
#include <glm/glm.hpp>

namespace wl {

// Compile et gere un programme shader GLSL (vertex + fragment).
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    // Charge depuis deux fichiers GLSL. Renvoie false en cas d'erreur de compilation.
    bool loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath);

    void bind() const;
    void unbind() const;

    void setMat4(const std::string& name, const glm::mat4& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setFloat(const std::string& name, float value) const;
    void setInt(const std::string& name, int value) const;

    uint32_t id() const { return m_id; }

private:
    static uint32_t compileStage(uint32_t type, const std::string& source, const std::string& label);
    int uniformLocation(const std::string& name) const;

    uint32_t m_id = 0;
};

} // namespace wl
