#version 450 core

// Billboard 2D (style Daggerfall) : un quad par instance, oriente face a la
// camera mais maintenu vertical (debout sur le sol). L'instance fournit sa
// position au sol (espace modele), sa taille et son type (tuile d'atlas).
layout(location = 0) in vec2 aQuad;    // coin du quad : x in [-0.5,0.5], y in [0,1]
layout(location = 1) in vec2 aUV;      // uv dans la tuile [0,1]
layout(location = 2) in vec3 aInstPos; // base du prop (espace modele, sur le relief)
layout(location = 3) in float aSize;   // taille (unites monde)
layout(location = 4) in float aType;   // index de tuile dans l'atlas

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform vec3 uCameraPos;  // position camera (monde)
uniform float uMaxDist;   // distance d'affichage (au-dela : invisible)
uniform int uAtlasCols;   // atlas carre : cols == rows

out vec2 vUV;

void main() {
    vec3 center = vec3(uModel * vec4(aInstPos, 1.0));
    vec3 up = normalize(center);              // "haut" local = normale a la sphere
    vec3 toCam = uCameraPos - center;
    float dist = length(toCam);
    toCam /= max(dist, 1e-5);
    vec3 right = normalize(cross(up, toCam)); // horizontale face a la camera

    // Fondu/retrecissement avec la distance -> les props lointains disparaissent.
    float fade = 1.0 - smoothstep(uMaxDist * 0.55, uMaxDist, dist);
    float size = aSize * fade;

    vec3 wp = center + right * (aQuad.x * size) + up * (aQuad.y * size);
    gl_Position = uViewProj * vec4(wp, 1.0);

    float col = mod(aType, float(uAtlasCols));
    float row = floor(aType / float(uAtlasCols));
    vUV = (vec2(col, row) + aUV) / float(uAtlasCols);
}
