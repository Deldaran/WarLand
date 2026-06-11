#version 450 core

in vec3 vNormal;
in vec3 vWorldPos;
in float vElevation;
in vec3 vUnitPos;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;     // direction normalisee vers le soleil
uniform float uSeaLevel;

out vec4 FragColor;

// Couleurs de base des biomes
const vec3 OCEAN_DEEP  = vec3(0.03, 0.10, 0.28);
const vec3 OCEAN_SHALLOW = vec3(0.08, 0.35, 0.55);
const vec3 BEACH       = vec3(0.78, 0.72, 0.50);
const vec3 GRASS       = vec3(0.20, 0.48, 0.20);
const vec3 FOREST      = vec3(0.13, 0.33, 0.15);
const vec3 ROCK        = vec3(0.40, 0.35, 0.30);
const vec3 SNOW        = vec3(0.92, 0.94, 0.97);

vec3 biomeColor(float e, float latitude) {
    vec3 color;
    if (e < uSeaLevel) {
        float depth = clamp((uSeaLevel - e) / 0.6, 0.0, 1.0);
        color = mix(OCEAN_SHALLOW, OCEAN_DEEP, depth);
    } else {
        float h = (e - uSeaLevel) / (1.0 - uSeaLevel); // 0 cote -> 1 sommet
        if (h < 0.04)      color = BEACH;
        else if (h < 0.30) color = mix(GRASS, FOREST, h / 0.30);
        else if (h < 0.65) color = mix(FOREST, ROCK, (h - 0.30) / 0.35);
        else               color = mix(ROCK, SNOW, (h - 0.65) / 0.35);

        // Neige aux poles (haute latitude) meme a basse altitude
        float polar = smoothstep(0.65, 0.95, latitude);
        color = mix(color, SNOW, polar * 0.85);
    }
    return color;
}

void main() {
    vec3 normal = normalize(vNormal);
    float latitude = abs(vUnitPos.y); // 0 a l'equateur, 1 aux poles

    vec3 albedo = biomeColor(vElevation, latitude);

    // Eclairage diffus + ambiant (soleil directionnel)
    float diffuse = max(dot(normal, uSunDir), 0.0);
    float ambient = 0.18;
    vec3 lit = albedo * (ambient + 0.95 * diffuse);

    // Speculaire discret sur les oceans
    if (vElevation < uSeaLevel) {
        vec3 viewDir = normalize(uCameraPos - vWorldPos);
        vec3 halfDir = normalize(uSunDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 48.0);
        lit += vec3(0.6) * spec;
    }

    FragColor = vec4(lit, 1.0);
}
