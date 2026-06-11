#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vColor;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vColor = aColor;
    // Normale en espace monde (modele uniforme -> pas besoin de la transposee inverse)
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uViewProj * worldPos;
}
