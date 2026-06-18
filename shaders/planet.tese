#version 450 core

// Evaluation shader : surface lissee par PN-triangles (interpolation courbe via
// les normales -> plus de facettes) + bruit de detail fin (visible seulement
// quand la tessellation est elevee, donc pres de la camera).

layout(triangles, fractional_odd_spacing, ccw) in;

in vec3 tcPos[];
in vec3 tcNormal[];
in float tcElev[];

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vNormal;
out vec3 vWorldPos;
out float vElevation;
out vec3 vUnitPos;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float vnoise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                   mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                   mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { s += a * vnoise(p); p *= 2.1; a *= 0.5; }
    return s;
}

void main() {
    float u = gl_TessCoord.x, v = gl_TessCoord.y, w = gl_TessCoord.z;

    vec3 P0 = tcPos[0], P1 = tcPos[1], P2 = tcPos[2];
    vec3 N0 = tcNormal[0], N1 = tcNormal[1], N2 = tcNormal[2];

    // --- Points de controle PN (surface de Bezier cubique) ---
    vec3 b300 = P0, b030 = P1, b003 = P2;
    vec3 b210 = (2.0*P0 + P1 - dot(P1 - P0, N0) * N0) / 3.0;
    vec3 b120 = (2.0*P1 + P0 - dot(P0 - P1, N1) * N1) / 3.0;
    vec3 b021 = (2.0*P1 + P2 - dot(P2 - P1, N1) * N1) / 3.0;
    vec3 b012 = (2.0*P2 + P1 - dot(P1 - P2, N2) * N2) / 3.0;
    vec3 b102 = (2.0*P2 + P0 - dot(P0 - P2, N2) * N2) / 3.0;
    vec3 b201 = (2.0*P0 + P2 - dot(P2 - P0, N0) * N0) / 3.0;
    vec3 E = (b210 + b120 + b021 + b012 + b102 + b201) / 6.0;
    vec3 Vc = (P0 + P1 + P2) / 3.0;
    vec3 b111 = E + (E - Vc) / 2.0;

    vec3 pos =
        b300 * (u*u*u) + b030 * (v*v*v) + b003 * (w*w*w) +
        3.0 * (b210 * (u*u*v) + b120 * (u*v*v) + b021 * (v*v*w) +
               b012 * (v*w*w) + b102 * (u*w*w) + b201 * (u*u*w)) +
        6.0 * b111 * (u*v*w);

    // --- Normales PN (champ quadratique) ---
    vec3 n200 = N0, n020 = N1, n002 = N2;
    float v01 = 2.0 * dot(P1 - P0, N0 + N1) / dot(P1 - P0, P1 - P0);
    vec3 n110 = normalize(N0 + N1 - v01 * (P1 - P0));
    float v12 = 2.0 * dot(P2 - P1, N1 + N2) / dot(P2 - P1, P2 - P1);
    vec3 n011 = normalize(N1 + N2 - v12 * (P2 - P1));
    float v20 = 2.0 * dot(P0 - P2, N2 + N0) / dot(P0 - P2, P0 - P2);
    vec3 n101 = normalize(N2 + N0 - v20 * (P0 - P2));
    vec3 nrm = normalize(n200*(u*u) + n020*(v*v) + n002*(w*w) +
                         n110*(2.0*u*v) + n011*(2.0*v*w) + n101*(2.0*w*u));

    float elev = tcElev[0]*u + tcElev[1]*v + tcElev[2]*w;

    // --- Detail fin (rugosite) : ajoute du relief la ou c'est tres tesselle ---
    float d = (fbm(pos * 45.0) - 0.5) * 0.006;
    pos += nrm * d;

    vec4 worldPos = uModel * vec4(pos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * nrm;
    vElevation = elev;
    vUnitPos = normalize(pos);
    gl_Position = uViewProj * worldPos;
}
