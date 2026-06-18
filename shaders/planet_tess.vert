#version 450 core

// Etage sommet pour le pipeline tesselle : passe les attributs (espace objet)
// au control shader. Le deplacement/projection se font dans l'evaluation shader.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aElev;

out vec3 vcPos;
out vec3 vcNormal;
out float vcElev;

void main() {
    vcPos = aPos;
    vcNormal = aNormal;
    vcElev = aElev;
}
