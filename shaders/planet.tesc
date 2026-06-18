#version 450 core

// Control shader : fixe le niveau de tessellation de chaque arete selon sa
// TAILLE A L'ECRAN -> detail fin pres de la camera, grossier au loin (LOD).

layout(vertices = 3) out;

in vec3 vcPos[];
in vec3 vcNormal[];
in float vcElev[];

out vec3 tcPos[];
out vec3 tcNormal[];
out float tcElev[];

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform vec2 uScreen;   // taille de la fenetre en pixels
uniform float uMaxTess; // niveau de tessellation max

// Longueur d'arete projetee a l'ecran (en pixels) -> niveau de tess.
float edgeTess(vec3 a, vec3 b) {
    vec4 ca = uViewProj * uModel * vec4(a, 1.0);
    vec4 cb = uViewProj * uModel * vec4(b, 1.0);
    if (ca.w <= 0.0 && cb.w <= 0.0) return 1.0; // arete derriere la camera
    vec2 sa = (ca.xy / max(ca.w, 1e-4)) * 0.5 * uScreen;
    vec2 sb = (cb.xy / max(cb.w, 1e-4)) * 0.5 * uScreen;
    float len = distance(sa, sb);
    return clamp(len / 16.0, 1.0, uMaxTess); // ~16 px par segment
}

void main() {
    tcPos[gl_InvocationID] = vcPos[gl_InvocationID];
    tcNormal[gl_InvocationID] = vcNormal[gl_InvocationID];
    tcElev[gl_InvocationID] = vcElev[gl_InvocationID];

    if (gl_InvocationID == 0) {
        // L'arete i est opposee au sommet i.
        float e0 = edgeTess(vcPos[1], vcPos[2]);
        float e1 = edgeTess(vcPos[2], vcPos[0]);
        float e2 = edgeTess(vcPos[0], vcPos[1]);
        gl_TessLevelOuter[0] = e0;
        gl_TessLevelOuter[1] = e1;
        gl_TessLevelOuter[2] = e2;
        gl_TessLevelInner[0] = max(e0, max(e1, e2));
    }
}
