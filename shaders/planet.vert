#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aElevation;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vNormal;
out vec3 vWorldPos;
out float vElevation;
out vec3 vUnitPos; // direction depuis le centre (pour la latitude)

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    vElevation = aElevation;
    vUnitPos = normalize(aPos);
    gl_Position = uViewProj * worldPos;
}
