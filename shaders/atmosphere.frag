#version 450 core

in vec3 vWorldPos;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec3 uPlanetCenter;

out vec4 FragColor;

const vec3 ATMO_COLOR = vec3(0.35, 0.55, 1.0);

void main() {
    // Normale de la coque atmospherique (sphere centree sur la planete).
    vec3 normal = normalize(vWorldPos - uPlanetCenter);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);

    // Rim : maximal au limbe (la ou la vue rase la surface).
    float rim = 1.0 - max(dot(viewDir, normal), 0.0);
    float intensity = pow(rim, 2.5);

    // L'atmosphere est plus lumineuse du cote eclaire par le soleil.
    float sunFacing = max(dot(normal, uSunDir), 0.0);
    intensity *= 0.4 + 0.6 * sunFacing;

    FragColor = vec4(ATMO_COLOR * intensity, intensity);
}
