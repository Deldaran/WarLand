#version 450 core

// Nuages volumetriques pilotes par le CYCLE DE L'EAU simule : la couverture
// nuageuse provient d'une texture (uCloudTex, en lat/lon planete-fixe) calculee
// par la simulation climatique (evaporation -> vents -> precipitation). Un bruit
// 3D ajoute le detail haute frequence. Les nuages tournent avec la planete et
// evoluent reellement (pas de pulsation), s'assombrissent quand ils sont epais.

in vec3 vWorldPos;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform float uMorphTime; // temps reel : fait "vivre"/deformer les nuages
uniform float uPlanetSpin;
uniform int uSteps;
uniform float uFade;
uniform float uPlanetRadius;
uniform float uCloudInner;
uniform float uCloudOuter;
uniform float uDensity;
uniform sampler2D uCloudTex; // couverture nuageuse simulee (0..1)

out vec4 FragColor;

const float PI = 3.14159265359;

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
    for (int i = 0; i < 3; ++i) { s += a * vnoise(p); p *= 2.0; a *= 0.5; }
    return s;
}

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

float cloudDensity(vec3 pos, out float cover) {
    float r = length(pos);
    float h = (r - uCloudInner) / (uCloudOuter - uCloudInner);
    cover = 0.0;
    if (h < 0.0 || h > 1.0) return 0.0;
    float vertical = smoothstep(0.0, 0.15, h) * smoothstep(1.0, 0.65, h);

    vec3 pdir = toPlanetFrame(normalize(pos)); // repere planete-fixe
    float lat = asin(clamp(pdir.y, -1.0, 1.0));
    float lon = atan(pdir.z, pdir.x);
    vec2 uv = vec2((lon + PI) / (2.0 * PI), (lat + PI * 0.5) / PI);
    cover = texture(uCloudTex, uv).r; // couverture simulee (cycle de l'eau)
    if (cover < 0.02) return 0.0;      // ciel clair -> on saute le bruit (perf)

    // Detail haute frequence qui se DEFORME en temps reel (nuages vivants) :
    // la couverture (ou) vient du climat (lente), mais la forme (volutes) morphe
    // sur des secondes via uMorphTime -> un nuage evolue "en quelques heures".
    vec3 sp = pdir * 9.0 + vec3(uMorphTime * 0.02, uMorphTime * 0.012, uMorphTime * 0.05);
    float n = fbm(sp);
    float d = smoothstep(1.0 - cover, 1.0 - cover + 0.45, n) * cover;
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
        float cover;
        float dens = cloudDensity(pos, cover);
        if (dens > 0.001) {
            float lc;
            float ld = cloudDensity(pos + uSunDir * 0.035, lc); // 1 echantillon (perf)
            float light = exp(-ld * 3.0);
            float a = dens * dt * uDensity;
            // Nuages epais (forte couverture) = plus sombres (orage).
            vec3 baseCol = mix(vec3(1.0), vec3(0.28, 0.30, 0.36), smoothstep(0.5, 1.0, cover));
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
