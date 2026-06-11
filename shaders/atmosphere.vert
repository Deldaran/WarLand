#version 450 core

layout(location = 0) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vWorldPos;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    gl_Position = uViewProj * worldPos;
}
