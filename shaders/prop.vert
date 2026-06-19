#version 450 core

// Billboard 2D (style Daggerfall) : un quad par instance, oriente face a la
// camera mais maintenu vertical (debout sur le sol). L'instance fournit sa
// position au sol (espace modele), sa taille et sa couche de texture array.
layout(location = 0) in vec2 aQuad;    // coin du quad : x in [-0.5,0.5], y in [0,1]
layout(location = 1) in vec2 aUV;      // uv dans le sprite [0,1]
layout(location = 2) in vec3 aInstPos; // base du prop (espace modele, sur le relief)
layout(location = 3) in float aSize;   // taille (unites monde)
layout(location = 4) in float aLayer;  // couche dans la texture array

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform vec3 uCameraPos;    // position camera (0 en rendu camera-relative)
uniform vec3 uPlanetCenter; // centre de la planete (meme repere que center)
uniform float uMaxDist;     // distance d'affichage (au-dela : invisible)

out vec2 vUV;
flat out float vLayer;

void main() {
    vec3 center = vec3(uModel * vec4(aInstPos, 1.0));
    vec3 up = normalize(center - uPlanetCenter); // "haut" = normale a la sphere
    vec3 toCam = uCameraPos - center;
    float dist = length(toCam);
    toCam /= max(dist, 1e-5);
    vec3 right = normalize(cross(up, toCam)); // horizontale face a la camera

    // Retrecissement avec la distance -> les props lointains disparaissent.
    float fade = 1.0 - smoothstep(uMaxDist * 0.55, uMaxDist, dist);
    float size = aSize * fade;

    vec3 wp = center + right * (aQuad.x * size) + up * (aQuad.y * size);
    gl_Position = uViewProj * vec4(wp, 1.0);

    vUV = aUV;
    vLayer = aLayer;
}
