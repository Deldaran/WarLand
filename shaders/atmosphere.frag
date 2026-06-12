#version 450 core

// Atmosphere realiste : fine couche autour de la planete. La brillance vient de
// la LONGUEUR DE TRAVERSEE du rayon dans l'air (l'horizon brille car on regarde
// a travers plus d'atmosphere), avec une densite qui decroit avec l'altitude et
// un terminateur jour/nuit doux. Donne un mince liseré bleu, pas une grosse bulle.

in vec3 vWorldPos;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform float uPlanetRadius;
uniform float uAtmoRadius;
uniform float uStrength;

out vec4 FragColor;

const vec3 ATMO_COLOR = vec3(0.35, 0.55, 1.0);

vec2 raySphere(vec3 ro, vec3 rd, float R) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - R * R;
    float h = b * b - c;
    if (h < 0.0) return vec2(1.0, -1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

void main() {
    vec3 ro = uCameraPos;
    vec3 rd = normalize(vWorldPos - uCameraPos);

    vec2 atmo = raySphere(ro, rd, uAtmoRadius);
    if (atmo.y < 0.0) discard;
    float tNear = max(atmo.x, 0.0);
    float tFar = atmo.y;

    // La planete bloque l'air situe derriere elle.
    vec2 planet = raySphere(ro, rd, uPlanetRadius);
    bool hitPlanet = planet.x > 0.0;
    if (hitPlanet) tFar = min(tFar, planet.x);
    if (tFar <= tNear) discard;

    // Longueur traversee dans l'atmosphere (max au limbe -> liseré lumineux).
    float through = tFar - tNear;

    // Densite ~ exponentielle avec l'altitude (echantillon au milieu du segment).
    vec3 mid = ro + rd * (tNear + tFar) * 0.5;
    float shell = max(0.0001, uAtmoRadius - uPlanetRadius);
    float alt = clamp((length(mid) - uPlanetRadius) / shell, 0.0, 1.0);
    float density = exp(-alt * 3.0);

    float optical = through * density * uStrength;

    // Eclairage : cote jour brillant, terminateur doux, cote nuit sombre.
    float sun = dot(normalize(mid), uSunDir);
    float dayNight = smoothstep(-0.25, 0.35, sun);

    float intensity = optical * (0.15 + 0.85 * dayNight);
    intensity = clamp(intensity, 0.0, 1.0);

    FragColor = vec4(ATMO_COLOR * intensity, intensity);
}
