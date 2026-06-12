#version 450 core

// Nuages volumetriques : raymarching d'un bruit 3D (FBM) dans une coquille
// spherique autour de la planete, avec auto-ombrage par le soleil. Le nombre
// de pas (uSteps) est pilote par le LOD, et uFade fait disparaitre les nuages
// quand la camera s'approche du sol.

in vec3 vWorldPos;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform float uTime;
uniform int uSteps;          // pas de raymarching (LOD)
uniform float uFade;         // 0 = nuages caches (zoom sol), 1 = visibles
uniform float uPlanetRadius; // pour occlure les nuages derriere la planete
uniform float uCloudInner;
uniform float uCloudOuter;
uniform float uCoverage;     // seuil de couverture nuageuse
uniform float uDensity;

out vec4 FragColor;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float vnoise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                   mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                   mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { s += a * vnoise(p); p *= 2.0; a *= 0.5; }
    return s;
}

// Intersection rayon / sphere centree a l'origine. x = entree, y = sortie.
vec2 raySphere(vec3 ro, vec3 rd, float R) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - R * R;
    float h = b * b - c;
    if (h < 0.0) return vec2(1.0, -1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

float cloudDensity(vec3 pos) {
    float r = length(pos);
    float h = (r - uCloudInner) / (uCloudOuter - uCloudInner);
    if (h < 0.0 || h > 1.0) return 0.0;
    // Fond / sommet de la couche adoucis.
    float vertical = smoothstep(0.0, 0.15, h) * smoothstep(1.0, 0.65, h);
    vec3 dir = normalize(pos);
    vec3 sp = dir * 5.0 + vec3(uTime * 0.015, 0.0, uTime * 0.010); // vent
    float n = fbm(sp);
    float d = smoothstep(uCoverage, uCoverage + 0.25, n);
    return d * vertical;
}

void main() {
    vec3 ro = uCameraPos;
    vec3 rd = normalize(vWorldPos - uCameraPos);

    vec2 outer = raySphere(ro, rd, uCloudOuter);
    if (outer.y < 0.0) discard; // rayon hors de la coquille

    float tStart = max(outer.x, 0.0);
    float tEnd = outer.y;

    // Occlusion par la planete : on ne marche pas au-dela de sa surface.
    vec2 planet = raySphere(ro, rd, uPlanetRadius);
    if (planet.x > 0.0) tEnd = min(tEnd, planet.x);
    if (tEnd <= tStart) discard;

    int steps = uSteps;
    float dt = (tEnd - tStart) / float(steps);
    // Decalage aleatoire pour casser le banding.
    float t = tStart + dt * hash(vWorldPos * 91.7);

    float transmittance = 1.0;
    vec3 col = vec3(0.0);

    for (int i = 0; i < steps; ++i) {
        vec3 pos = ro + rd * t;
        float dens = cloudDensity(pos);
        if (dens > 0.001) {
            // Auto-ombrage : densite vers le soleil (2 echantillons).
            float ld = cloudDensity(pos + uSunDir * 0.02)
                     + cloudDensity(pos + uSunDir * 0.05);
            float light = exp(-ld * 2.0);
            float a = dens * dt * uDensity;
            vec3 lit = mix(vec3(0.45, 0.50, 0.62), vec3(1.0), light);
            col += transmittance * a * lit;
            transmittance *= exp(-a);
            if (transmittance < 0.02) break;
        }
        t += dt;
    }

    float alpha = (1.0 - transmittance) * uFade;
    if (alpha < 0.003) discard;
    FragColor = vec4(col, alpha);
}
