#version 450 core

// Nuages volumetriques : raymarching d'un bruit 3D (FBM) dans une coquille
// spherique autour de la planete. Les nuages TOURNENT avec la planete (le bruit
// est echantillonne en repere planete-fixe) et EVOLUENT dans le temps in-game :
// des fronts de tempete (wl_storm) les epaississent et les assombrissent.
// uSteps = LOD ; uFade = disparition au zoom sol.

in vec3 vWorldPos;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform float uTime;         // temps in-game (jours) pour l'evolution meteo
uniform float uPlanetSpin;   // rotation propre de la planete (rad)
uniform int uSteps;
uniform float uFade;
uniform float uPlanetRadius;
uniform float uCloudInner;
uniform float uCloudOuter;
uniform float uCoverage;
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

// Champ de tempete (DOIT etre identique cote CPU pour aligner nuages et meteo
// du sol) : quelques bandes mobiles -> fronts meteo, renvoie ~0 (clair) a 1 (orage).
float wl_storm(vec3 d, float t) {
    float v = sin(d.x * 2.7 + t * 0.08)
            + sin(d.z * 3.1 - t * 0.06)
            + sin((d.x + d.z) * 2.0 + t * 0.05)
            + 0.8 * sin(d.y * 4.0 + t * 0.09);
    return clamp(v * 0.22 + 0.5, 0.0, 1.0);
}

// Passe une direction monde en repere planete-fixe (annule la rotation propre).
vec3 toPlanetFrame(vec3 wdir) {
    float cs = cos(uPlanetSpin), sn = sin(uPlanetSpin);
    return vec3(wdir.x * cs - wdir.z * sn, wdir.y, wdir.x * sn + wdir.z * cs);
}

vec2 raySphere(vec3 ro, vec3 rd, float R) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - R * R;
    float h = b * b - c;
    if (h < 0.0) return vec2(1.0, -1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

float cloudDensity(vec3 pos, out float storm) {
    float r = length(pos);
    float h = (r - uCloudInner) / (uCloudOuter - uCloudInner);
    storm = 0.0;
    if (h < 0.0 || h > 1.0) return 0.0;
    float vertical = smoothstep(0.0, 0.15, h) * smoothstep(1.0, 0.65, h);
    vec3 pdir = toPlanetFrame(normalize(pos)); // tourne avec la planete
    storm = wl_storm(pdir, uTime);
    vec3 sp = pdir * 5.0 + vec3(uTime * 0.002, 0.0, 0.0); // leger vent
    float n = fbm(sp);
    float cov = uCoverage - storm * 0.18; // plus de nuages dans les tempetes
    float d = smoothstep(cov, cov + 0.25, n);
    return d * vertical;
}

void main() {
    vec3 ro = uCameraPos;
    vec3 rd = normalize(vWorldPos - uCameraPos);

    vec2 outer = raySphere(ro, rd, uCloudOuter);
    if (outer.y < 0.0) discard;
    float tStart = max(outer.x, 0.0);
    float tEnd = outer.y;
    vec2 planet = raySphere(ro, rd, uPlanetRadius);
    if (planet.x > 0.0) tEnd = min(tEnd, planet.x);
    if (tEnd <= tStart) discard;

    int steps = uSteps;
    float dt = (tEnd - tStart) / float(steps);
    float t = tStart + dt * hash(vWorldPos * 91.7);

    float transmittance = 1.0;
    vec3 col = vec3(0.0);

    for (int i = 0; i < steps; ++i) {
        vec3 pos = ro + rd * t;
        float storm;
        float dens = cloudDensity(pos, storm);
        if (dens > 0.001) {
            float ls;
            float ld = cloudDensity(pos + uSunDir * 0.02, ls)
                     + cloudDensity(pos + uSunDir * 0.05, ls);
            float light = exp(-ld * 2.0);
            float a = dens * dt * uDensity;
            // Orage -> nuages plus sombres.
            vec3 baseCol = mix(vec3(1.0), vec3(0.30, 0.31, 0.35), storm * 0.8);
            vec3 lit = baseCol * (0.35 + 0.65 * light);
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
