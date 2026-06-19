#version 450 core

layout(location = 0) in vec3 aPos;

uniform mat4 uViewProj; // proj * (rotation seule de la vue) -> fond a l'infini

out vec3 vDir;

void main() {
    vDir = aPos;                 // direction echantillonnee dans le cubemap
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
